#include "uart_task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "driver/uart.h"
#include "esp_log.h"
#include "motor_task.h"
#include "sensor_task.h"

static const char *TAG = "UART_TELEM";

static void uart_send_str(const char *str) {
    if (str) {
        uart_write_bytes(UART_PORT_PC, str, strlen(str));
    }
}

/**
 * @brief Emite el encabezado del bloque CSV indicando metadatos y columnas
 *        de los sensores autodetectados en el TCA9548A.
 */
static void send_csv_header(void) {
    char line[256];
    snprintf(line, sizeof(line),
             "\r\n$SEISMIC_BEGIN,FREQ_HZ=%.2f,AMP_MM=%.2f,DUR_S=%.1f,ACTIVE_MASK=0x%02X,ACTIVE_COUNT=%u\r\n",
             g_sys_ctx.target_freq_hz,
             g_sys_ctx.target_amp_mm,
             g_sys_ctx.duration_s,
             g_sys_ctx.active_imu_mask,
             g_sys_ctx.active_imu_count);
    uart_send_str(line);

    // Construir cabecera dinámica: timestamp_ms,cmd_pos_mm y luego S1..S6 activos
    strcpy(line, "$CSV_HDR,timestamp_ms,cmd_pos_mm");
    for (uint8_t ch = 0; ch < MAX_IMU_SENSORS; ch++) {
        if (g_sys_ctx.active_imu_mask & (1U << ch)) {
            char col[48];
            snprintf(col, sizeof(col), ",S%u_ax_g,S%u_ay_g,S%u_az_g", ch + 1, ch + 1, ch + 1);
            strncat(line, col, sizeof(line) - strlen(line) - 1);
        }
    }
    strncat(line, "\r\n", sizeof(line) - strlen(line) - 1);
    uart_send_str(line);
}

static void send_csv_sample(const sensor_sample_t *s) {
    char line[320];
    int len = snprintf(line, sizeof(line), "$DATA,%lu,%.3f",
                       (unsigned long)s->timestamp_ms,
                       s->commanded_pos_mm);

    for (uint8_t ch = 0; ch < MAX_IMU_SENSORS; ch++) {
        if (s->active_mask & (1U << ch)) {
            len += snprintf(line + len, sizeof(line) - (size_t)len,
                            ",%.4f,%.4f,%.4f",
                            s->ax_g[ch], s->ay_g[ch], s->az_g[ch]);
        }
    }
    snprintf(line + len, sizeof(line) - (size_t)len, "\r\n");
    uart_send_str(line);
}

/**
 * @brief Procesa un comando enmarcado tipo `<CMD:VAL>` o línea limpia.
 */
static void process_command(char *cmd) {
    // Limpiar espacios, saltos de línea y paréntesis angulares `<` `>`
    while (*cmd == ' ' || *cmd == '\r' || *cmd == '\n' || *cmd == '<') cmd++;
    size_t n = strlen(cmd);
    while (n > 0 && (cmd[n - 1] == ' ' || cmd[n - 1] == '\r' || cmd[n - 1] == '\n' || cmd[n - 1] == '>')) {
        cmd[--n] = '\0';
    }
    if (n == 0) return;

    char resp[128];

    if ( strcmp(cmd, "START") == 0 ) {
        if (motor_start_excitation(g_sys_ctx.target_freq_hz, g_sys_ctx.target_amp_mm, g_sys_ctx.duration_s)) {
            uart_send_str("<ACK:START>\r\n");
        } else {
            uart_send_str("<ERR:START_REJECTED>\r\n");
        }
    } else if ( strcmp(cmd, "STOP") == 0 ) {
        motor_stop_excitation(false);
        uart_send_str("<ACK:STOP>\r\n");
    } else if ( strcmp(cmd, "ESTOP") == 0 ) {
        motor_stop_excitation(true);
        uart_send_str("<ACK:ESTOP>\r\n");
    } else if ( strcmp(cmd, "RESET") == 0 ) {
        g_sys_ctx.estop_triggered = false;
        fsm_set_state(SYS_IDLE);
        uart_send_str("<ACK:RESET>\r\n");
    } else if ( strcmp(cmd, "SCAN") == 0 ) {
        if (g_sys_ctx.state != SYS_RUNNING) {
            uint8_t mask = sensor_scan_and_calibrate();
            snprintf(resp, sizeof(resp), "<ACK:SCAN:MASK=0x%02X:COUNT=%u>\r\n",
                     mask, g_sys_ctx.active_imu_count);
            uart_send_str(resp);
        } else {
            uart_send_str("<ERR:BUSY_RUNNING>\r\n");
        }
    } else if ( strcmp(cmd, "STATUS") == 0 ) {
        snprintf(resp, sizeof(resp),
                 "<STATUS:STATE=%s:FREQ=%.2f:AMP=%.2f:DUR=%.1f:SENS_MASK=0x%02X:PGA=%.3f>\r\n",
                 fsm_state_to_str(g_sys_ctx.state),
                 g_sys_ctx.target_freq_hz,
                 g_sys_ctx.target_amp_mm,
                 g_sys_ctx.duration_s,
                 g_sys_ctx.active_imu_mask,
                 g_sys_ctx.pga_mesa_g);
        uart_send_str(resp);
    } else if ( strncmp(cmd, "SET:FREQ:", 9) == 0 ) {
        float val = strtof(cmd + 9, NULL);
        if (val >= SEISMIC_MIN_FREQ_HZ && val <= SEISMIC_MAX_FREQ_HZ) {
            g_sys_ctx.target_freq_hz = val;
            snprintf(resp, sizeof(resp), "<ACK:FREQ:%.2f>\r\n", val);
            uart_send_str(resp);
        } else {
            uart_send_str("<ERR:FREQ_OUT_OF_RANGE>\r\n");
        }
    } else if ( strncmp(cmd, "SET:AMP:", 8) == 0 ) {
        float val = strtof(cmd + 8, NULL);
        if (val >= SEISMIC_MIN_AMP_MM && val <= SEISMIC_MAX_AMP_MM) {
            g_sys_ctx.target_amp_mm = val;
            snprintf(resp, sizeof(resp), "<ACK:AMP:%.2f>\r\n", val);
            uart_send_str(resp);
        } else {
            uart_send_str("<ERR:AMP_OUT_OF_RANGE>\r\n");
        }
    } else if ( strncmp(cmd, "SET:DUR:", 8) == 0 ) {
        float val = strtof(cmd + 8, NULL);
        if (val >= 1.0f && val <= 300.0f) {
            g_sys_ctx.duration_s = val;
            snprintf(resp, sizeof(resp), "<ACK:DUR:%.1f>\r\n", val);
            uart_send_str(resp);
        } else {
            uart_send_str("<ERR:DUR_OUT_OF_RANGE>\r\n");
        }
    } else {
        snprintf(resp, sizeof(resp), "<ERR:UNKNOWN_CMD:%s>\r\n", cmd);
        uart_send_str(resp);
    }
}

void uart_task(void *arg) {
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_PC, UART_RX_BUF_SIZE * 2, UART_TX_BUF_SIZE, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_PC, &uart_config));

    ESP_LOGI(TAG, "Protocolo UART listo a %d baudios. Formato <CMD:VAL>", UART_BAUD_RATE);

    uint8_t rx_buf[128];
    char cmd_line[128];
    size_t cmd_pos = 0;
    bool csv_stream_open = false;

    while (1) {
        // 1. Procesar recepción de comandos no bloqueante
        int rx_len = uart_read_bytes(UART_PORT_PC, rx_buf, sizeof(rx_buf) - 1, pdMS_TO_TICKS(10));
        if (rx_len > 0) {
            for (int i = 0; i < rx_len; i++) {
                char c = (char)rx_buf[i];
                if (c == '\n' || c == '\r' || c == '>') {
                    if (cmd_pos > 0) {
                        cmd_line[cmd_pos] = '\0';
                        process_command(cmd_line);
                        cmd_pos = 0;
                    }
                } else if (c == '<') {
                    cmd_pos = 0; // Inicio de nueva trama <CMD:VAL>
                } else if (cmd_pos < sizeof(cmd_line) - 1) {
                    cmd_line[cmd_pos++] = c;
                }
            }
        }

        // 2. Gestionar transmisión de la trama sísmica para el generador de Excel
        if (g_sys_ctx.state == SYS_RUNNING && !csv_stream_open) {
            xQueueReset(cola_sensores);
            send_csv_header();
            csv_stream_open = true;
        }

        if (csv_stream_open) {
            sensor_sample_t sample;
            while (xQueueReceive(cola_sensores, &sample, 0) == pdTRUE) {
                send_csv_sample(&sample);
            }

            if (g_sys_ctx.state != SYS_RUNNING) {
                char footer[96];
                snprintf(footer, sizeof(footer), "$SEISMIC_END,PGA_MESA_G=%.4f\r\n", g_sys_ctx.pga_mesa_g);
                uart_send_str(footer);
                csv_stream_open = false;
            }
        }
    }
}
