#ifndef UI_TASK_H
#define UI_TASK_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "sensor_task.h"
#include "motor_task.h"

// Colas/flags externos (definidos en otros módulos)
//extern QueueHandle_t cola_sensores;
extern volatile bool motor_run;

void ui_task(void *arg);

#endif
