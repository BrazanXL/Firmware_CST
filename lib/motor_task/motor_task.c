#include "motor_task.h"
#include <math.h>
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MOTOR_CTRL";

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// Estado interno del generador de pasos en hardware (ISR a 10 kHz)
static esp_timer_handle_t s_step_timer = NULL;
static volatile int32_t s_current_steps = 0;
static volatile int32_t s_target_steps = 0;
static volatile int64_t s_start_time_us = 0;
static volatile float s_active_freq_hz = SEISMIC_DEFAULT_FREQ_HZ;
static volatile float s_active_amp_mm = SEISMIC_DEFAULT_AMP_MM;
static volatile float s_active_duration_s = SEISMIC_DEFAULT_DURATION_S;
static volatile float s_steps_per_mm = MOTOR_STEPS_PER_MM_DEFAULT;
static volatile bool s_motion_active = false;

/**
 * @brief ISR del botón físico de Parada de Emergencia (E-STOP).
 *        Deshabilita inmediatamente el driver DM856 por hardware.
 */
static void IRAM_ATTR estop_isr_handler(void *arg) {
    gpio_set_level(PIN_MOTOR_ENA, MOTOR_ENA_DISABLE_LEVEL);
    gpio_set_level(PIN_MOTOR_STEP, 0);
    s_motion_active = false;
    g_sys_ctx.estop_triggered = true;
    g_sys_ctx.state = SYS_ERROR;
}

/**
 * @brief Callback de temporizador de alta resolución (`esp_timer` a 10 kHz / 100 us).
 *        Calcula la posición armónica objetivo x(t) = A(t) * sin(2*pi*f*t) con envolvente
 *        trapezoidal de rampa y emite los pulsos STEP/DIR de forma determinista sin
 *        bloquear ninguna tarea de FreeRTOS ni disparar el Task Watchdog.
 */
static void IRAM_ATTR step_generator_isr_cb(void *arg) {
    if (!s_motion_active || g_sys_ctx.estop_triggered) {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    float t_sec = (float)(now_us - s_start_time_us) * 1e-6f;

    if (t_sec >= s_active_duration_s) {
        s_motion_active = false;
        return;
    }

    // Envolvente de rampa suave al inicio y al final para proteger la mecánica
    float envelope = 1.0f;
    if (t_sec < SEISMIC_RAMP_TIME_S) {
        envelope = t_sec / SEISMIC_RAMP_TIME_S;
    } else if ((s_active_duration_s - t_sec) < SEISMIC_RAMP_TIME_S) {
        envelope = (s_active_duration_s - t_sec) / SEISMIC_RAMP_TIME_S;
        if (envelope < 0.0f) envelope = 0.0f;
    }

    // Posición instantánea deseada en pasos: x(t) = env * A * sin(2*pi*f*t)
    float desired_mm = envelope * s_active_amp_mm * sinf(2.0f * M_PI * s_active_freq_hz * t_sec);
    s_target_steps = (int32_t)lroundf(desired_mm * s_steps_per_mm);

    int32_t diff = s_target_steps - s_current_steps;
    if (diff != 0) {
        bool dir_positive = (diff > 0);
        gpio_set_level(PIN_MOTOR_DIR, dir_positive ? 1 : 0);
        gpio_set_level(PIN_MOTOR_STEP, 1);
        esp_rom_delay_us(MOTOR_PULSE_WIDTH_US);
        gpio_set_level(PIN_MOTOR_STEP, 0);

        s_current_steps += (dir_positive ? 1 : -1);
    }
}

void motor_control_init(void) {
    // 1. Configurar pines de salida hacia el DM856
    gpio_config_t io_out = {
        .pin_bit_mask = (1ULL << PIN_MOTOR_STEP) | (1ULL << PIN_MOTOR_DIR) | (1ULL << PIN_MOTOR_ENA),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_out);

    gpio_set_level(PIN_MOTOR_STEP, 0);
    gpio_set_level(PIN_MOTOR_DIR, 0);
    gpio_set_level(PIN_MOTOR_ENA, MOTOR_ENA_DISABLE_LEVEL); // Motor libre al arrancar

    // 2. Configurar parada de emergencia por interrupción hardware (PIN_BTN_ESTOP)
    gpio_config_t io_estop = {
        .pin_bit_mask = (1ULL << PIN_BTN_ESTOP),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    gpio_config(&io_estop);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_BTN_ESTOP, estop_isr_handler, NULL);

    // 3. Crear temporizador periódico de hardware (100 us = 10 kHz)
    const esp_timer_create_args_t timer_args = {
        .callback = &step_generator_isr_cb,
        .name = "stepper_hw_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_step_timer));
    ESP_LOGI(TAG, "Controlador DM856 inicializado (STEP=%d, DIR=%d, ENA=%d)",
             PIN_MOTOR_STEP, PIN_MOTOR_DIR, PIN_MOTOR_ENA);
}

bool motor_start_excitation(float freq_hz, float amp_mm, float duration_s) {
    if (g_sys_ctx.estop_triggered || g_sys_ctx.state == SYS_ERROR) {
        ESP_LOGE(TAG, "No se puede iniciar: Parada de emergencia o estado de ERROR activo");
        return false;
    }

    if (freq_hz < SEISMIC_MIN_FREQ_HZ || freq_hz > SEISMIC_MAX_FREQ_HZ ||
        amp_mm < SEISMIC_MIN_AMP_MM || amp_mm > SEISMIC_MAX_AMP_MM ||
        duration_s <= 0.5f) {
        ESP_LOGW(TAG, "Parámetros fuera de rango seguro: F=%.2f Hz, A=%.2f mm, T=%.1f s",
                 freq_hz, amp_mm, duration_s);
        return false;
    }

    // Verificar que la velocidad pico (2*pi*f*A*steps_per_mm) no supere la tasa del timer (10 kHz)
    float max_step_rate = 2.0f * M_PI * freq_hz * amp_mm * s_steps_per_mm;
    float timer_max_rate = 1000000.0f / (float)MOTOR_CONTROL_TICK_US;
    if (max_step_rate > timer_max_rate * 0.90f) {
        ESP_LOGW(TAG, "Velocidad pico requerida (%.0f pasos/s) excede límite seguro (%.0f pasos/s)",
                 max_step_rate, timer_max_rate * 0.90f);
    }

    s_active_freq_hz = freq_hz;
    s_active_amp_mm = amp_mm;
    s_active_duration_s = duration_s;
    s_current_steps = 0;
    s_target_steps = 0;

    // Habilitar driver DM856 (Activo en bajo)
    gpio_set_level(PIN_MOTOR_ENA, MOTOR_ENA_ACTIVE_LEVEL);
    s_start_time_us = esp_timer_get_time();
    s_motion_active = true;

    esp_timer_stop(s_step_timer);
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_step_timer, MOTOR_CONTROL_TICK_US));

    fsm_set_state(SYS_RUNNING);
    ESP_LOGI(TAG, "Excitación Sísmica INICIADA: F=%.2f Hz | A=%.2f mm | T=%.1f s",
             freq_hz, amp_mm, duration_s);
    return true;
}

void motor_stop_excitation(bool emergency) {
    s_motion_active = false;
    if (s_step_timer) {
        esp_timer_stop(s_step_timer);
    }
    gpio_set_level(PIN_MOTOR_STEP, 0);
    gpio_set_level(PIN_MOTOR_ENA, MOTOR_ENA_DISABLE_LEVEL);

    if (emergency) {
        g_sys_ctx.estop_triggered = true;
        fsm_set_state(SYS_ERROR);
        ESP_LOGE(TAG, "¡PARADA DE EMERGENCIA ACTIVADA!");
    } else {
        if (g_sys_ctx.state == SYS_RUNNING) {
            fsm_set_state(SYS_IDLE);
        }
        ESP_LOGI(TAG, "Movimiento detenido de forma segura.");
    }
}

float motor_get_current_pos_mm(void) {
    return (float)s_current_steps / s_steps_per_mm;
}

void motor_task(void *arg) {
    motor_control_init();

    while (1) {
        if (s_motion_active) {
            int64_t now_us = esp_timer_get_time();
            float elapsed = (float)(now_us - s_start_time_us) * 1e-6f;
            g_sys_ctx.elapsed_s = elapsed;
            g_sys_ctx.current_pos_mm = motor_get_current_pos_mm();

            if (elapsed >= s_active_duration_s) {
                motor_stop_excitation(false);
                buzzer_beep(2, 120, 80); // Doble pitido indicando fin de ensayo
            }
        } else if (g_sys_ctx.state == SYS_RUNNING && !s_motion_active) {
            // El temporizador terminó el ciclo
            motor_stop_excitation(false);
            buzzer_beep(2, 120, 80);
        }

        vTaskDelay(pdMS_TO_TICKS(20)); // Supervisión a 50 Hz sin ocupar CPU
    }
}
