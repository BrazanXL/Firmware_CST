#include "ui_task.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "motor_task.h"
#include "sensor_task.h"

static const char *TAG = "UI_LOCAL";

static esp_lcd_panel_handle_t s_panel = NULL;
static uint16_t *s_stripe_buf = NULL; // Buffer parcial DMA: 240 x 20 px = 9,600 bytes

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    // Invertir bytes para envío SPI Big-Endian hacia ST7789
    return (uint16_t)((v >> 8) | (v << 8));
}

#define COL_BG          rgb565(15, 20, 30)
#define COL_HEADER_IDLE rgb565(0, 120, 215)
#define COL_HEADER_RUN  rgb565(220, 120, 0)
#define COL_HEADER_ERR  rgb565(210, 20, 20)
#define COL_SENSOR_ON   rgb565(20, 185, 65)
#define COL_SENSOR_OFF  rgb565(65, 70, 80)
#define COL_CARD_ACTIVE rgb565(40, 90, 160)
#define COL_CARD_NORM   rgb565(30, 38, 55)
#define COL_WHITE       rgb565(255, 255, 255)

/**
 * @brief Rellena un rectángulo usando transferencias por franjas DMA de 20 líneas
 *        para no consumir 115 KB de SRAM estática.
 */
static void lcd_fill_rect(int x0, int y0, int w, int h, uint16_t color) {
    if (!s_panel || !s_stripe_buf || w <= 0 || h <= 0) return;

    int y_end = y0 + h;
    for (int y = y0; y < y_end; y += LCD_STRIPE_LINES) {
        int lines = (y + LCD_STRIPE_LINES <= y_end) ? LCD_STRIPE_LINES : (y_end - y);
        int total_px = w * lines;
        for (int i = 0; i < total_px; i++) {
            s_stripe_buf[i] = color;
        }
        esp_lcd_panel_draw_bitmap(s_panel, x0, y, x0 + w, y + lines, s_stripe_buf);
    }
}

/**
 * @brief Dibuja una barra de progreso horizontal simple en una región.
 */
static void lcd_draw_bar(int x, int y, int w, int h, float ratio, uint16_t fg, uint16_t bg) {
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    int fill_w = (int)(w * ratio);
    if (fill_w > 0) lcd_fill_rect(x, y, fill_w, h, fg);
    if (fill_w < w) lcd_fill_rect(x + fill_w, y, w - fill_w, h, bg);
}

static void ui_controls_init(void) {
    // Botón START con pull-up interno
    gpio_config_t io_pullup = {
        .pin_bit_mask = (1ULL << PIN_BTN_START),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_pullup);

    // Pines Input-Only del ESP32 (GPIO 34, 35, 36, 39) para Encoder y Sensor Home
    gpio_config_t io_in_only = {
        .pin_bit_mask = (1ULL << PIN_ENC_A) | (1ULL << PIN_ENC_B) |
                        (1ULL << PIN_ENC_SW) | (1ULL << PIN_SENSOR_HOME),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_in_only);
}

static void ui_lcd_init(void) {
    s_stripe_buf = (uint16_t *)heap_caps_malloc(LCD_H_RES * LCD_STRIPE_LINES * sizeof(uint16_t), MALLOC_CAP_DMA);

    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_LCD_SCLK,
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_STRIPE_LINES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, &io));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_endian = LCD_RGB_ENDIAN_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_config, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    gpio_reset_pin(PIN_LCD_BL);
    gpio_set_direction(PIN_LCD_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_LCD_BL, 1);

    lcd_fill_rect(0, 0, LCD_H_RES, LCD_V_RES, COL_BG);
    ESP_LOGI(TAG, "Pantalla ST7789 inicializada con buffer DMA ligero (%u bytes)",
             (unsigned)(LCD_H_RES * LCD_STRIPE_LINES * sizeof(uint16_t)));
}

/**
 * @brief Actualiza las 4 zonas visuales del Dashboard en el ST7789:
 *        - Zona 1 (Y: 0..35): Barra de estado (Azul=IDLE, Naranja=RUNNING, Rojo=ERROR)
 *        - Zona 2 (Y: 44..78): Matriz de los 6 sensores S1..S6 (Verde=Conectado, Gris=No detectado)
 *        - Zona 3 (Y: 88..188): Barras proporcionales de Frecuencia, Amplitud y Duración
 *        - Zona 4 (Y: 196..236): Barra de progreso de tiempo / PGA de la mesa
 */
static void ui_render_dashboard(void) {
    // 1. Barra Superior de Estado
    uint16_t hdr_col = COL_HEADER_IDLE;
    if (g_sys_ctx.state == SYS_RUNNING) hdr_col = COL_HEADER_RUN;
    else if (g_sys_ctx.state == SYS_ERROR) hdr_col = COL_HEADER_ERR;
    lcd_fill_rect(0, 0, LCD_H_RES, 34, hdr_col);

    // 2. Matriz de Autodetección de los 6 Sensores (S1 a S6)
    int box_w = 32;
    int gap = 6;
    int start_x = 12;
    for (uint8_t ch = 0; ch < MAX_IMU_SENSORS; ch++) {
        bool active = (g_sys_ctx.active_imu_mask & (1U << ch)) != 0;
        int x = start_x + ch * (box_w + gap);
        lcd_fill_rect(x, 44, box_w, 28, active ? COL_SENSOR_ON : COL_SENSOR_OFF);
    }

    // 3. Tarjetas/Barras de Consigna (Frecuencia, Amplitud, Duración)
    uint8_t sel = g_sys_ctx.ui_selected_field;

    // Frecuencia (0.1 .. 10 Hz)
    lcd_fill_rect(8, 84, 6, 26, (sel == 0) ? COL_WHITE : COL_BG);
    lcd_draw_bar(20, 84, 208, 26,
                 g_sys_ctx.target_freq_hz / SEISMIC_MAX_FREQ_HZ,
                 (sel == 0) ? COL_CARD_ACTIVE : COL_HEADER_IDLE, COL_CARD_NORM);

    // Amplitud (0.5 .. 25 mm)
    lcd_fill_rect(8, 120, 6, 26, (sel == 1) ? COL_WHITE : COL_BG);
    lcd_draw_bar(20, 120, 208, 26,
                 g_sys_ctx.target_amp_mm / SEISMIC_MAX_AMP_MM,
                 (sel == 1) ? COL_CARD_ACTIVE : COL_HEADER_IDLE, COL_CARD_NORM);

    // Duración (1 .. 60 s vista nominal)
    lcd_fill_rect(8, 156, 6, 26, (sel == 2) ? COL_WHITE : COL_BG);
    lcd_draw_bar(20, 156, 208, 26,
                 g_sys_ctx.duration_s / 60.0f,
                 (sel == 2) ? COL_CARD_ACTIVE : COL_HEADER_IDLE, COL_CARD_NORM);

    // 4. Barra Inferior: Progreso de ejecución sísmica
    float progress = (g_sys_ctx.duration_s > 0.0f && g_sys_ctx.state == SYS_RUNNING)
                     ? (g_sys_ctx.elapsed_s / g_sys_ctx.duration_s)
                     : 0.0f;
    lcd_draw_bar(12, 202, 216, 24, progress, COL_SENSOR_ON, COL_CARD_NORM);
}

void ui_task(void *arg) {
    ui_controls_init();
    ui_lcd_init();

    int last_enc_a = gpio_get_level(PIN_ENC_A);
    int last_enc_sw = gpio_get_level(PIN_ENC_SW);
    int last_btn_start = gpio_get_level(PIN_BTN_START);
    uint32_t last_render_ms = 0;

    while (1) {
        // Lectura del Encoder Rotatorio para ajustar Frecuencia, Amplitud o Duración
        int enc_a = gpio_get_level(PIN_ENC_A);
        if (enc_a != last_enc_a && enc_a == 0) {
            int enc_b = gpio_get_level(PIN_ENC_B);
            float dir = (enc_b != enc_a) ? 1.0f : -1.0f;

            if (g_sys_ctx.state == SYS_IDLE || g_sys_ctx.state == SYS_CONFIG) {
                if (g_sys_ctx.ui_selected_field == 0) {
                    float f = g_sys_ctx.target_freq_hz + dir * 0.2f;
                    if (f < SEISMIC_MIN_FREQ_HZ) f = SEISMIC_MIN_FREQ_HZ;
                    if (f > SEISMIC_MAX_FREQ_HZ) f = SEISMIC_MAX_FREQ_HZ;
                    g_sys_ctx.target_freq_hz = f;
                } else if (g_sys_ctx.ui_selected_field == 1) {
                    float a = g_sys_ctx.target_amp_mm + dir * 0.5f;
                    if (a < SEISMIC_MIN_AMP_MM) a = SEISMIC_MIN_AMP_MM;
                    if (a > SEISMIC_MAX_AMP_MM) a = SEISMIC_MAX_AMP_MM;
                    g_sys_ctx.target_amp_mm = a;
                } else {
                    float d = g_sys_ctx.duration_s + dir * 1.0f;
                    if (d < 2.0f) d = 2.0f;
                    if (d > 120.0f) d = 120.0f;
                    g_sys_ctx.duration_s = d;
                }
            }
        }
        last_enc_a = enc_a;

        // Pulsador del Encoder: Cambia entre Frecuencia (0), Amplitud (1) y Duración (2)
        int enc_sw = gpio_get_level(PIN_ENC_SW);
        if (enc_sw == 0 && last_enc_sw == 1) {
            g_sys_ctx.ui_selected_field = (g_sys_ctx.ui_selected_field + 1) % 3;
            buzzer_beep(1, 30, 0);
        }
        last_enc_sw = enc_sw;

        // Botón físico START / STOP
        int btn_start = gpio_get_level(PIN_BTN_START);
        if (btn_start == 0 && last_btn_start == 1) {
            if (g_sys_ctx.state == SYS_RUNNING) {
                motor_stop_excitation(false);
                buzzer_beep(1, 150, 0);
            } else if (g_sys_ctx.state == SYS_IDLE || g_sys_ctx.state == SYS_CONFIG) {
                // Cuenta regresiva acústica de seguridad antes de mover la mesa
                buzzer_beep(3, 80, 120);
                motor_start_excitation(g_sys_ctx.target_freq_hz,
                                       g_sys_ctx.target_amp_mm,
                                       g_sys_ctx.duration_s);
            }
        }
        last_btn_start = btn_start;

        // Refrescar pantalla cada 150 ms (~6.6 FPS) para no saturar SPI
        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (now_ms - last_render_ms >= 150) {
            ui_render_dashboard();
            last_render_ms = now_ms;
        }

        vTaskDelay(pdMS_TO_TICKS(15));
    }
}
