#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "bsp_config.h"
#include "motor_task.h"
#include "sensor_task.h"
#include "uart_task.h"
#include "ui_task.h"

static const char *TAG = "MAIN_FSM";

// Contexto global de la máquina de estados
volatile system_context_t g_sys_ctx = {
    .state = SYS_BOOT,
    .target_freq_hz = SEISMIC_DEFAULT_FREQ_HZ,
    .target_amp_mm = SEISMIC_DEFAULT_AMP_MM,
    .duration_s = SEISMIC_DEFAULT_DURATION_S,
    .elapsed_s = 0.0f,
    .current_pos_mm = 0.0f,
    .pga_mesa_g = 0.0f,
    .active_imu_mask = 0x00,
    .active_imu_count = 0,
    .ui_selected_field = 0,
    .estop_triggered = false
};

// Cola global de muestras sísmicas (sensor_task -> uart_task)
QueueHandle_t cola_sensores = NULL;

const char* fsm_state_to_str(system_state_t state) {
    switch (state) {
        case SYS_BOOT:     return "BOOT";
        case SYS_SELFTEST: return "SELFTEST";
        case SYS_IDLE:     return "IDLE";
        case SYS_CONFIG:   return "CONFIG";
        case SYS_RUNNING:  return "RUNNING";
        case SYS_ERROR:    return "ERROR";
        case SYS_SHUTDOWN: return "SHUTDOWN";
        default:           return "UNKNOWN";
    }
}

void fsm_set_state(system_state_t new_state) {
    system_state_t prev = g_sys_ctx.state;
    if (prev != new_state) {
        g_sys_ctx.state = new_state;
        ESP_LOGI(TAG, "Transición FSM: %s -> %s",
                 fsm_state_to_str(prev), fsm_state_to_str(new_state));
    }
}

void buzzer_beep(uint8_t count, uint16_t duration_ms, uint16_t pause_ms) {
    for (uint8_t i = 0; i < count; i++) {
        gpio_set_level(PIN_BUZZER, 1);
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        gpio_set_level(PIN_BUZZER, 0);
        if (pause_ms > 0 && i + 1 < count) {
            vTaskDelay(pdMS_TO_TICKS(pause_ms));
        }
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "=== Iniciando Firmware Mesa Sísmica CUC v%s (ESP-IDF + FreeRTOS) ===", FIRMWARE_VERSION_STRING);

    // Configurar pin del Buzzer
    gpio_reset_pin(PIN_BUZZER);
    gpio_set_direction(PIN_BUZZER, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_BUZZER, 0);

    // Crear cola de muestras sísmicas (capacidad 64 muestras de hasta 6 sensores)
    cola_sensores = xQueueCreate(64, sizeof(sensor_sample_t));

    // Crear tareas en núcleos dedicados del ESP32 para determinismo en tiempo real:
    // - Core 1 (APP): Control de motor y adquisición I2C de los 6 MPU6050
    // - Core 0 (PRO): Comunicaciones UART/Excel e Interfaz gráfica ST7789
    xTaskCreatePinnedToCore(motor_task,  "motor_task",  4096, NULL, 12, NULL, 1);
    xTaskCreatePinnedToCore(sensor_task, "sensor_task", 4096, NULL, 10, NULL, 1);
    xTaskCreatePinnedToCore(uart_task,   "uart_task",   4096, NULL,  8, NULL, 0);
    xTaskCreatePinnedToCore(ui_task,     "ui_task",     4096, NULL,  6, NULL, 0);

    buzzer_beep(1, 100, 0);
}
