#include "uart_task.h"
#include "utilities.h"
#include <string.h>
#include <stdio.h>

#define UART_PORT UART_NUM_0
#define BUF_SIZE 1024

extern QueueHandle_t cola_uart;

void uart_task(void *arg) {
    QueueHandle_t uart_queue;

    // Inicializar UART usando tu librería
    uart_init(UART_PORT, 115200, BUF_SIZE * 2, BUF_SIZE * 2, 50, &uart_queue, ESP_INTR_FLAG_LEVEL1);
    uart_set_pin(UART_PORT, 1, 3, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    uint8_t data[BUF_SIZE];

    while (1) {
        uart_event_t event;
        if (xQueueReceive(uart_queue, (void *)&event, (TickType_t)portMAX_DELAY)) {
            switch (event.type) {
                case UART_DATA:
                    // Leer datos recibidos
                    uart_receive(UART_PORT, data, event.size);
                    data[event.size] = '\0';

                    printf("UART Recv: %s\n", data);

                    // Enviar a la cola para otras tareas
                    xQueueSend(cola_uart, data, 0);

                    // Responder con eco
                    uart_transmit(UART_PORT, (const char *)data, strlen((char *)data));
                    break;

                default:
                    break;
            }
        }
    }
}
