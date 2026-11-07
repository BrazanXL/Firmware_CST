#ifndef UI_TASK_H
#define UI_TASK_H

#include "bsp_config.h"

/**
 * @brief Tarea de Interfaz Local (Pantalla ST7789 + Encoder Rotatorio + Botón START).
 *        Renderiza en 4 zonas (Estado, Matriz de 6 Sensores Autodetectados S1..S6,
 *        Consignas de Frecuencia/Amplitud/Duración y Telemetría PGA) usando un
 *        buffer parcial en DMA de solo 9.6 KB en lugar de 115 KB.
 */
void ui_task(void *arg);

#endif // UI_TASK_H
