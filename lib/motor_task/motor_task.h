#ifndef MOTOR_TASK_H
#define MOTOR_TASK_H

#include <stdbool.h>
#include <stdint.h>
#include "bsp_config.h"

/**
 * @brief Inicializa los pines del driver DM856, la interrupción física de parada
 *        de emergencia (E-STOP) y el temporizador determinista de hardware.
 */
void motor_control_init(void);

/**
 * @brief Inicia la excitación armónica/sísmica con rampa suave de aceleración.
 * @param freq_hz Frecuencia objetivo en Hz (SEISMIC_MIN_FREQ_HZ .. SEISMIC_MAX_FREQ_HZ)
 * @param amp_mm  Amplitud pico en mm (SEISMIC_MIN_AMP_MM .. SEISMIC_MAX_AMP_MM)
 * @param duration_s Duración total del ensayo en segundos
 * @return true si los parámetros son válidos y el movimiento inició.
 */
bool motor_start_excitation(float freq_hz, float amp_mm, float duration_s);

/**
 * @brief Detiene suavemente o de inmediato la excitación del motor.
 * @param emergency Si es true, corta ENA inmediatamente y enclava SYS_ERROR.
 */
void motor_stop_excitation(bool emergency);

/**
 * @brief Obtiene la posición teórica instantánea de la mesa en milímetros.
 */
float motor_get_current_pos_mm(void);

/**
 * @brief Tarea supervisora de movimiento (verifica duración, rampa, límites y E-STOP).
 */
void motor_task(void *arg);

#endif // MOTOR_TASK_H
