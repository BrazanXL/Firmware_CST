#ifndef SENSOR_TASK_H
#define SENSOR_TASK_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef struct {
    float ax, ay, az;
    float gx, gy, gz;
} sensor_data_t;

extern QueueHandle_t cola_sensores;

void sensor_task(void *arg);

#endif
