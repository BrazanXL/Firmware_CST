#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "motor_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "freertos/queue.h" 

#define PIN_STEP 21
#define PIN_DIR  19

volatile bool motor_run = false;
volatile int pulse_width_us = 500; // Ajustar velocidad

void motor_task(void *arg) {
    gpio_set_direction(PIN_STEP, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_DIR, GPIO_MODE_OUTPUT);

    gpio_set_level(PIN_DIR, 1);

    while (1) {
        if (motor_run) {
            gpio_set_level(PIN_STEP, 1);
            esp_rom_delay_us(pulse_width_us);
            gpio_set_level(PIN_STEP, 0);
            esp_rom_delay_us(pulse_width_us);
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}
