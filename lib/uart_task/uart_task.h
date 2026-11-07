#ifndef UART_TASK_H
#define UART_TASK_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "bsp_config.h"

/**
 * @brief Tarea de comunicación serial y telemetría estructurada hacia Excel.
 *        - Escucha comandos con formato `<START>`, `<STOP>`, `<SET:FREQ:2.5>`,
 *          `<SET:AMP:5.0>`, `<SET:DUR:10.0>`, `<SCAN>`, `<STATUS>`.
 *        - Durante `SYS_RUNNING`, emite encabezados y filas CSV sincronizadas
 *          con los sensores activos (1 a 6) para el script de captura a Excel.
 */
void uart_task(void *arg);

#endif // UART_TASK_H
