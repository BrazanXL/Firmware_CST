#include "sensor_task.h"
#include <stdio.h>

void sensor_task(void *arg) {
    sensor_data_t sensor_data;

    while (1) {
        // Simulación de datos
        sensor_data.ax = 1.0;
        sensor_data.ay = 0.0;
        sensor_data.az = -1.0;

        xQueueSend(cola_sensores, &sensor_data, 0);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
