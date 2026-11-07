#ifndef BSP_CONFIG_H
#define BSP_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "driver/uart.h"

/**
 * ============================================================================
 * 1. PINOUT ETAPA DE POTENCIA — MOTOR NEMA 23 + DRIVER DM856
 * ============================================================================
 */
#define PIN_MOTOR_STEP      GPIO_NUM_25   // Señal PUL/STEP hacia DM856
#define PIN_MOTOR_DIR       GPIO_NUM_26   // Señal DIR hacia DM856
#define PIN_MOTOR_ENA       GPIO_NUM_27   // Señal ENA hacia DM856 (Activo en BAJO)

#define MOTOR_ENA_ACTIVE_LEVEL   0        // DM856 habilitado en nivel bajo
#define MOTOR_ENA_DISABLE_LEVEL  1        // DM856 deshabilitado (libre/seguro)

/**
 * Parámetros mecánicos y cinemáticos nominales (ajustables tras metrología)
 * Nota: 1342.1f pasos/mm obtenidos en ensayo de banco (8000 pasos -> 5.958 mm).
 */
#define MOTOR_STEPS_PER_MM_DEFAULT   1342.1f
#define MOTOR_PULSE_WIDTH_US         5       // Ancho de pulso STEP en microsegundos (DM856 >= 2.5 us)
#define MOTOR_CONTROL_TICK_US        100     // Resolución del temporizador de hardware (10 kHz)

// Límites seguros de operación de la mesa sísmica
#define SEISMIC_MIN_FREQ_HZ          0.1f
#define SEISMIC_MAX_FREQ_HZ          10.0f
#define SEISMIC_DEFAULT_FREQ_HZ      2.0f

#define SEISMIC_MIN_AMP_MM           0.5f
#define SEISMIC_MAX_AMP_MM           25.0f
#define SEISMIC_DEFAULT_AMP_MM       5.0f

#define SEISMIC_DEFAULT_DURATION_S   10.0f
#define SEISMIC_RAMP_TIME_S          1.5f    // Rampa suave de inicio/parada (evita pérdida de pasos)

/**
 * ============================================================================
 * 2. RED DE HASTA 6 SENSORES INERCIALES — TCA9548A + 6x MPU6050 (I2C)
 * ============================================================================
 */
#define I2C_MASTER_PORT     I2C_NUM_0
#define PIN_I2C_SDA         GPIO_NUM_21
#define PIN_I2C_SCL         GPIO_NUM_22
#define I2C_MASTER_FREQ_HZ  400000           // Fast Mode 400 kHz para muestreo sísmico rápido

#define TCA9548A_I2C_ADDR   0x70             // Dirección base del multiplexor I2C TCA9548A
#define MPU6050_I2C_ADDR    0x68             // Dirección I2C de cada MPU6050 (AD0 = GND)
#define MAX_IMU_SENSORS     6                // S1 (Canal 0: Mesa), S2..S6 (Canales 1..5: Estructura)
#define IMU_SAMPLE_PERIOD_MS 10              // 100 Hz de muestreo sísmico

/**
 * ============================================================================
 * 3. INTERFAZ LOCAL — PANTALLA TFT ST7789 (SPI2)
 * ============================================================================
 */
#define LCD_SPI_HOST        SPI2_HOST
#define PIN_LCD_SCLK        GPIO_NUM_18
#define PIN_LCD_MOSI        GPIO_NUM_23
#define PIN_LCD_CS          GPIO_NUM_5
#define PIN_LCD_DC          GPIO_NUM_16
#define PIN_LCD_RST         GPIO_NUM_17
#define PIN_LCD_BL          GPIO_NUM_4

#define LCD_H_RES           240
#define LCD_V_RES           240
#define LCD_STRIPE_LINES    20               // Buffer parcial en DMA (240*20*2 = 9.6 KB en lugar de 115 KB)

/**
 * ============================================================================
 * 4. CONTROLES FÍSICOS, PARADA DE EMERGENCIA, HOME Y BUZZER
 * ============================================================================
 */
#define PIN_BTN_ESTOP       GPIO_NUM_32      // Parada de emergencia física (Interrupción Pull-Up)
#define PIN_BTN_START       GPIO_NUM_33      // Botón físico START / PAUSE (Pull-Up)
#define PIN_ENC_A           GPIO_NUM_34      // Encoder rotatorio Canal A (Input-only, requiere pull-up ext)
#define PIN_ENC_B           GPIO_NUM_35      // Encoder rotatorio Canal B (Input-only, requiere pull-up ext)
#define PIN_ENC_SW          GPIO_NUM_39      // Pulsador del Encoder (VN, Input-only, requiere pull-up ext)
#define PIN_SENSOR_HOME     GPIO_NUM_36      // Sensor óptico de Home / Límite (VP, Input-only)
#define PIN_BUZZER          GPIO_NUM_15      // Buzzer activo para alertas de inicio/error

/**
 * ============================================================================
 * 5. COMUNICACIÓN SERIAL / EXPORTACIÓN DE DATOS A EXCEL (UART0)
 * ============================================================================
 */
#define UART_PORT_PC        UART_NUM_0
#define UART_BAUD_RATE      115200
#define UART_RX_BUF_SIZE    512
#define UART_TX_BUF_SIZE    1024

/**
 * ============================================================================
 * 6. MÁQUINA DE ESTADOS GLOBAL DEL SISTEMA (FSM)
 * ============================================================================
 */
typedef enum {
    SYS_BOOT = 0,       // Inicialización de periféricos
    SYS_SELFTEST,       // Escaneo automático de los 6 sensores en TCA9548A + Calibración Offset
    SYS_IDLE,           // En espera (listo para configurar o iniciar ensayo)
    SYS_CONFIG,         // Edición de Frecuencia / Amplitud / Duración en UI local
    SYS_RUNNING,        // Ejecutando excitación sísmica y transmitiendo trama CSV para Excel
    SYS_ERROR,          // Parada de emergencia o fallo enclavado
    SYS_SHUTDOWN        // Apagado seguro de etapa de potencia
} system_state_t;

typedef struct {
    system_state_t state;
    float target_freq_hz;
    float target_amp_mm;
    float duration_s;
    float elapsed_s;
    float current_pos_mm;
    float pga_mesa_g;                   // Peak Ground Acceleration medida en S1 (Mesa)
    uint8_t active_imu_mask;            // Bitmask (bits 0..5) de sensores MPU6050 detectados en PCB
    uint8_t active_imu_count;           // Cantidad total de sensores detectados (0 a 6)
    uint8_t ui_selected_field;          // 0: Frecuencia, 1: Amplitud, 2: Duración
    bool estop_triggered;
} system_context_t;

extern volatile system_context_t g_sys_ctx;

const char* fsm_state_to_str(system_state_t state);
void fsm_set_state(system_state_t new_state);
void buzzer_beep(uint8_t count, uint16_t duration_ms, uint16_t pause_ms);

#endif // BSP_CONFIG_H
