#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "uart_task.h"
#include "motor_task.h"
#include "sensor_task.h"
#include "ui_task.h"

// Colas globales
QueueHandle_t cola_uart;
QueueHandle_t cola_sensores;

void app_main(void) {
    // Crear colas
    cola_uart = xQueueCreate(10, sizeof(char) * 64);
    cola_sensores = xQueueCreate(10, sizeof(sensor_data_t));

    // Crear tareas
    xTaskCreate(uart_task, "uart_task", 4096, NULL, 10, NULL);
    xTaskCreate(motor_task, "motor_task", 4096, NULL, 12, NULL);
    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 8, NULL);
    xTaskCreate(ui_task, "ui_task", 4096, NULL, 6, NULL);
}
