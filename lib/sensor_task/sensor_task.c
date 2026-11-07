#include "sensor_task.h"
#include <string.h>
#include <math.h>
#include "driver/i2c.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "motor_task.h"

static const char *TAG = "IMU_MGR";

// Registros internos del MPU6050
#define MPU6050_REG_PWR_MGMT_1   0x6B
#define MPU6050_REG_SMPLRT_DIV   0x19
#define MPU6050_REG_CONFIG       0x1A
#define MPU6050_REG_ACCEL_CONFIG 0x1C
#define MPU6050_REG_ACCEL_XOUT_H 0x3B
#define MPU6050_REG_WHO_AM_I     0x75

#define MPU6050_LSB_PER_G_4G     8192.0f   // Escala +/- 4g para ensayos sísmicos

static float s_offset_ax[MAX_IMU_SENSORS] = {0};
static float s_offset_ay[MAX_IMU_SENSORS] = {0};
static float s_offset_az[MAX_IMU_SENSORS] = {0};
static bool s_i2c_initialized = false;

static esp_err_t i2c_bus_init(void) {
    if (s_i2c_initialized) return ESP_OK;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_PORT, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_PORT, conf.mode, 0, 0, 0));
    s_i2c_initialized = true;
    return ESP_OK;
}

/**
 * @brief Selecciona un canal (0..5) en el multiplexor I2C TCA9548A (0x70).
 */
static esp_err_t tca9548a_select_channel(uint8_t channel) {
    if (channel >= MAX_IMU_SENSORS) return ESP_ERR_INVALID_ARG;
    uint8_t mask = (1U << channel);
    return i2c_master_write_to_device(I2C_MASTER_PORT, TCA9548A_I2C_ADDR,
                                      &mask, 1, pdMS_TO_TICKS(15));
}

static esp_err_t mpu6050_write_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    return i2c_master_write_to_device(I2C_MASTER_PORT, MPU6050_I2C_ADDR,
                                      buf, sizeof(buf), pdMS_TO_TICKS(15));
}

static esp_err_t mpu6050_read_regs(uint8_t reg, uint8_t *data, size_t len) {
    return i2c_master_write_read_device(I2C_MASTER_PORT, MPU6050_I2C_ADDR,
                                        &reg, 1, data, len, pdMS_TO_TICKS(15));
}

/**
 * @brief Inicializa un MPU6050 en el canal actualmente seleccionado del TCA9548A.
 */
static bool mpu6050_probe_and_init(void) {
    uint8_t who_am_i = 0;
    if (mpu6050_read_regs(MPU6050_REG_WHO_AM_I, &who_am_i, 1) != ESP_OK) {
        return false;
    }
    // Aceptar variantes estándar MPU6050 / MPU6500 (0x68, 0x70, 0x72)
    if (who_am_i != 0x68 && who_am_i != 0x70 && who_am_i != 0x72) {
        return false;
    }

    // Despertar sensor, DLPF a 44 Hz (0x03) para filtrar vibración de alta frecuencia del NEMA23,
    // y rango de acelerómetro en +/- 4g (0x08).
    if (mpu6050_write_reg(MPU6050_REG_PWR_MGMT_1, 0x01) != ESP_OK) return false;
    mpu6050_write_reg(MPU6050_REG_CONFIG, 0x03);
    mpu6050_write_reg(MPU6050_REG_SMPLRT_DIV, 0x09);   // 1 kHz / (1 + 9) = 100 Hz
    mpu6050_write_reg(MPU6050_REG_ACCEL_CONFIG, 0x08); // +/- 4g
    return true;
}

static bool mpu6050_read_accel_g(float *ax, float *ay, float *az) {
    uint8_t raw[6];
    if (mpu6050_read_regs(MPU6050_REG_ACCEL_XOUT_H, raw, 6) != ESP_OK) {
        return false;
    }
    int16_t rx = (int16_t)((raw[0] << 8) | raw[1]);
    int16_t ry = (int16_t)((raw[2] << 8) | raw[3]);
    int16_t rz = (int16_t)((raw[4] << 8) | raw[5]);

    *ax = (float)rx / MPU6050_LSB_PER_G_4G;
    *ay = (float)ry / MPU6050_LSB_PER_G_4G;
    *az = (float)rz / MPU6050_LSB_PER_G_4G;
    return true;
}

uint8_t sensor_scan_and_calibrate(void) {
    i2c_bus_init();

    uint8_t mask = 0;
    uint8_t count = 0;
    memset(s_offset_ax, 0, sizeof(s_offset_ax));
    memset(s_offset_ay, 0, sizeof(s_offset_ay));
    memset(s_offset_az, 0, sizeof(s_offset_az));

    ESP_LOGI(TAG, "Escaneando multiplexor TCA9548A (0x%02X) para 6 canales MPU6050...", TCA9548A_I2C_ADDR);

    for (uint8_t ch = 0; ch < MAX_IMU_SENSORS; ch++) {
        if (tca9548a_select_channel(ch) == ESP_OK) {
            if (mpu6050_probe_and_init()) {
                mask |= (1U << ch);
                count++;
                ESP_LOGI(TAG, "  [OK] Sensor S%d detectado en canal %d del TCA9548A", ch + 1, ch);
            } else {
                ESP_LOGW(TAG, "  [--] Canal %d sin sensor MPU6050", ch);
            }
        }
    }

    // Calibración rápida de offset en reposo (32 muestras por sensor detectado)
    if (mask != 0) {
        const int cal_samples = 32;
        for (int n = 0; n < cal_samples; n++) {
            for (uint8_t ch = 0; ch < MAX_IMU_SENSORS; ch++) {
                if (mask & (1U << ch)) {
                    float ax = 0, ay = 0, az = 0;
                    if (tca9548a_select_channel(ch) == ESP_OK && mpu6050_read_accel_g(&ax, &ay, &az)) {
                        s_offset_ax[ch] += ax;
                        s_offset_ay[ch] += ay;
                        s_offset_az[ch] += az;
                    }
                }
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        for (uint8_t ch = 0; ch < MAX_IMU_SENSORS; ch++) {
            if (mask & (1U << ch)) {
                s_offset_ax[ch] /= (float)cal_samples;
                s_offset_ay[ch] /= (float)cal_samples;
                s_offset_az[ch] /= (float)cal_samples;
                ESP_LOGI(TAG, "  Offset S%d: ax=%.3fg, ay=%.3fg, az=%.3fg",
                         ch + 1, s_offset_ax[ch], s_offset_ay[ch], s_offset_az[ch]);
            }
        }
    } else {
        ESP_LOGW(TAG, "No se detectó el TCA9548A o ningún MPU6050. Operando sin sensores físicos.");
    }

    g_sys_ctx.active_imu_mask = mask;
    g_sys_ctx.active_imu_count = count;
    return mask;
}

void sensor_task(void *arg) {
    fsm_set_state(SYS_SELFTEST);
    sensor_scan_and_calibrate();
    fsm_set_state(SYS_IDLE);

    TickType_t last_wake = xTaskGetTickCount();
    int64_t run_start_us = 0;
    bool was_running = false;

    while (1) {
        bool is_running = (g_sys_ctx.state == SYS_RUNNING);
        if (is_running && !was_running) {
            run_start_us = esp_timer_get_time();
            g_sys_ctx.pga_mesa_g = 0.0f;
        }
        was_running = is_running;

        sensor_sample_t sample;
        memset(&sample, 0, sizeof(sample));

        int64_t now_us = esp_timer_get_time();
        sample.timestamp_ms = is_running
            ? (uint32_t)((now_us - run_start_us) / 1000ULL)
            : (uint32_t)(now_us / 1000ULL);
        sample.commanded_pos_mm = motor_get_current_pos_mm();
        sample.active_mask = g_sys_ctx.active_imu_mask;

        // Leer únicamente los sensores que fueron autodetectados en la PCB
        for (uint8_t ch = 0; ch < MAX_IMU_SENSORS; ch++) {
            if (sample.active_mask & (1U << ch)) {
                float ax = 0, ay = 0, az = 0;
                if (tca9548a_select_channel(ch) == ESP_OK && mpu6050_read_accel_g(&ax, &ay, &az)) {
                    sample.ax_g[ch] = ax - s_offset_ax[ch];
                    sample.ay_g[ch] = ay - s_offset_ay[ch];
                    sample.az_g[ch] = az - s_offset_az[ch];
                }
            }
        }

        // Actualizar PGA (Peak Ground Acceleration) del sensor S1 (Mesa, canal 0)
        if (sample.active_mask & 0x01) {
            float abs_ax = fabsf(sample.ax_g[0]);
            if (abs_ax > g_sys_ctx.pga_mesa_g) {
                g_sys_ctx.pga_mesa_g = abs_ax;
            }
        }

        // Si el ensayo está corriendo, enviar muestra a la cola para volcado UART -> Excel
        if (is_running && cola_sensores != NULL) {
            xQueueSend(cola_sensores, &sample, 0);
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(IMU_SAMPLE_PERIOD_MS));
    }
}
