#ifndef SENSOR_TASK_H
#define SENSOR_TASK_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "bsp_config.h"

/**
 * @brief Muestra sísmica sincronizada de hasta 6 sensores MPU6050 a través de TCA9548A.
 *        Canal 0 (S1): Mesa vibratoria
 *        Canales 1..5 (S2..S6): Niveles/puntos de la estructura de prueba
 */
typedef struct {
    uint32_t timestamp_ms;                  // Estampa de tiempo relativa al inicio del ensayo
    float commanded_pos_mm;                 // Posición teórica de consigna en la mesa (mm)
    uint8_t active_mask;                    // Bitmask de sensores presentes (bits 0..5)
    float ax_g[MAX_IMU_SENSORS];            // Aceleración eje X en g (ya compensada con offset)
    float ay_g[MAX_IMU_SENSORS];            // Aceleración eje Y en g
    float az_g[MAX_IMU_SENSORS];            // Aceleración eje Z en g
} sensor_sample_t;

extern QueueHandle_t cola_sensores;

/**
 * @brief Escanea los 6 canales del multiplexor TCA9548A, detecta qué sensores
 *        MPU6050 están físicamente conectados en la PCB y calibra sus offsets en reposo.
 * @return Bitmask de sensores activos (bit 0 = S1 ... bit 5 = S6).
 */
uint8_t sensor_scan_and_calibrate(void);

/**
 * @brief Tarea de adquisición sísmica periódica (100 Hz).
 */
void sensor_task(void *arg);

#endif // SENSOR_TASK_H
