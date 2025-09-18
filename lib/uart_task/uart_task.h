#ifndef UART_TASK_H
#define UART_TASK_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"

// Cola global para enviar/recibir mensajes UART
extern QueueHandle_t cola_uart;

// Prototipo de la tarea principal de UART
void uart_task(void *arg);

#endif // UART_TASK_H
