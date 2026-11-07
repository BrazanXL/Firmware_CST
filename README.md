# Firmware_CST — Sistema de Control y Adquisición para Mesa Sísmica (CUC)

![Versión de Firmware](https://img.shields.io/badge/Firmware-v3.4.52-blue)
![Framework](https://img.shields.io/badge/ESP--IDF-v5.x%20%2F%20FreeRTOS-red)
![MCU](https://img.shields.io/badge/MCU-ESP32%20DOIT%20DevKit%20V1-green)
![Licencia](https://img.shields.io/badge/License-MIT-yellow)

Firmware embebido en tiempo real desarrollado en **C nativo sobre ESP-IDF y FreeRTOS** para el control cinemático, instrumentación inercial multipunto y adquisición sísmica de la **Mesa Vibratoria Uniaxial de Laboratorio** de la **Universidad de la Costa (CUC)**, bajo criterios de referencia del reglamento **NSR-10**.

---

## 1. Arquitectura General del Sistema (`v3.4.52`)

El sistema opera sobre los dos núcleos del **ESP32 DOIT DevKit V1** mediante tareas dedicadas (`xTaskCreatePinnedToCore`), garantizando determinismo en la generación del movimiento sísmico sin interferencia de la interfaz gráfica ni del volcado de datos:

* **Core 1 (Tiempo Real y Control):**
  * [`motor_task`](lib/motor_task/motor_task.c) (Prioridad 12) + **Hardware Timer ISR (10 kHz / 100 µs):** Síntesis de trayectoria armónica $x(t) = E(t)\cdot A \sin(2\pi f t)$ con envolvente trapezoidal de rampa ($1.5\text{ s}$) para accionamiento del motor **NEMA 23** y driver **DM856**, más interrupción inmediata de parada de emergencia (`E-STOP`).
  * [`sensor_task`](lib/sensor_task/sensor_task.c) (Prioridad 10): Escaneo automático (*Plug & Play*) y muestreo sincronizado a **100 Hz** de **hasta 6 acelerómetros/giroscopios MPU6050** a través del multiplexor I²C **TCA9548A** (`0x70`) en PCB dedicada, con filtro digital pasa-bajos (DLPF a $44\text{ Hz}$), rango $\pm 4g$ y calibración automática de *offset* en reposo.
* **Core 0 (Interfaz Local y Telemetría):**
  * [`uart_task`](lib/uart_task/uart_task.c) (Prioridad 8): Intérprete del protocolo `<CMD:VAL>` y emisor de tramas estructuradas (`$SEISMIC_BEGIN`, `$CSV_HDR`, `$DATA`, `$SEISMIC_END`) hacia el generador automático de reportes en Excel ([`tools/seismic_excel_logger.py`](tools/seismic_excel_logger.py)).
  * [`ui_task`](lib/ui_task/ui_task.c) (Prioridad 6): Interfaz local autónoma sobre pantalla **TFT ST7789 (240×240)** con renderizado por franjas DMA ($9.6\text{ KB}$ de RAM), visualización de estado de los 6 sensores (`S1` a `S6`), ajuste por encoder rotatorio y alertas acústicas por buzzer.

---

## 2. Asignación de Pines (Pinout Principal en PCB `v3.4.52`)

Definido de forma centralizada en [`include/bsp_config.h`](include/bsp_config.h):

| Subsistema | Señal | GPIO ESP32 | Observaciones |
| :--- | :--- | :---: | :--- |
| **Driver DM856 (NEMA 23)** | `PUL / STEP` | `GPIO 25` | Tren de pulsos temporizado por hardware ($5\text{ }\mu\text{s}$) |
| | `DIR` | `GPIO 26` | Inversión de giro axial |
| | `ENA` | `GPIO 27` | Habilitación activa en **nivel bajo** (`0 = ON`, `1 = OFF`) |
| **Red IMU (TCA9548A + 6× MPU6050)** | `I2C_SDA` | `GPIO 21` | Bus I²C Fast-Mode ($400\text{ kHz}$), `TCA9548A @ 0x70` |
| | `I2C_SCL` | `GPIO 22` | Canales `0..5` → Sensores `S1` (Mesa) a `S6` (Estructura) |
| **Pantalla TFT ST7789 (SPI2)** | `SCLK / MOSI` | `GPIO 18 / 23` | Reloj y datos SPI DMA ($40\text{ MHz}$) |
| | `CS / DC / RST` | `GPIO 5 / 16 / 17` | Control de panel |
| | `BLK` | `GPIO 4` | Control de retroiluminación |
| **Seguridad y Controles Locales** | `BTN_ESTOP` | `GPIO 32` | Parada de emergencia por interrupción (`GPIO_INTR_NEGEDGE`) |
| | `BTN_START` | `GPIO 33` | Inicio/pausa con cuenta regresiva acústica |
| | `ENC_A / ENC_B / SW` | `GPIO 34 / 35 / 39` | Encoder rotatorio de selección de parámetros |
| | `SENSOR_HOME` | `GPIO 36` | Sensor óptico de referencia/límite |
| | `BUZZER` | `GPIO 15` | Indicador acústico de operación y alarma |

---

## 3. Protocolo Serie y Exportación Automática a Excel

### Comandos admitidos (`115200 8N1`)
* `<START>`: Inicia el ensayo sísmico con rampa suave y abre el volcado de datos.
* `<STOP>`: Detiene el ensayo de forma controlada.
* `<ESTOP>`: Dispara parada de emergencia inmediata y enclava estado `ERROR`.
* `<RESET>`: Libera el enclavamiento de error y regresa a `IDLE`.
* `<SCAN>`: Re-escanea los 6 canales del `TCA9548A` y recalibra *offsets* en reposo.
* `<STATUS>`: Devuelve estado de la FSM, consignas, máscara de sensores activos y PGA medido.
* `<SET:FREQ:2.5>` / `<SET:AMP:5.0>` / `<SET:DUR:10.0>`: Ajusta frecuencia ($\text{Hz}$), amplitud ($\text{mm}$) o duración ($\text{s}$).

### Generación del archivo Excel (`.xlsx`) con gráficas
```bash
pip install pyserial xlsxwriter
python tools/seismic_excel_logger.py --port COM3
```
Al finalizar cualquier movimiento (iniciado desde los botones físicos de la mesa o por comando), el script genera automáticamente en `ensayos_excel/` un libro `.xlsx` con los datos tabulados de los sensores detectados y las gráficas de **Desplazamiento vs. Tiempo** y **Aceleración por Nivel vs. Tiempo**.

---

## 4. Historial de Versiones y Trazabilidad (`Changelog`)

A continuación se documenta la evolución técnica del firmware desde la línea base `v1.9.2` hasta la versión de producción `v3.4.52`:

### **`v3.4.52` — (07/11/2026) — Arquitectura de Producción, Autodetección de 6 IMU y Exportación a Excel**
* **Reconstrucción integral sobre ESP-IDF + FreeRTOS:** Eliminación definitiva de librerías heredadas (`utilities.c`) y partición de tareas por afinidad de núcleo (`Core 0` / `Core 1`).
* **Soporte para hasta 6 sensores MPU6050 con autodetección (*Plug & Play*):** Escaneo dinámico de los canales `0..5` del multiplexor `TCA9548A` durante `SYS_SELFTEST`, ajustando automáticamente la máscara de muestreo y las columnas de telemetría según los sensores conectados en la PCB.
* **Controlador cinemático determinista:** Generación de perfil senoidal con rampas de aceleración/desaceleración de $1.5\text{ s}$ mediante ISR de temporizador de hardware a $10\text{ kHz}$ y calibración base de $1342.1\text{ pasos/mm}$.
* **Optimización de RAM en UI Local (`ST7789`):** Reducción del consumo de RAM de $115\text{ KB}$ a solo $9.6\text{ KB}$ mediante transferencias por franjas DMA (`240×20 px`), incorporando el panel de estado de los 6 sensores (`S1..S6`).
* **Automatización de reportes:** Integración de [`tools/seismic_excel_logger.py`](tools/seismic_excel_logger.py) para convertir las tramas `$SEISMIC_BEGIN ... $SEISMIC_END` directamente en libros de Excel (`.xlsx`) con gráficas nativas.

### **`v3.3.18` — (Octubre 2026) — Máquina de Estados (FSM) y Seguridad Hardware**
* Definición formal de los estados operativos (`SYS_BOOT`, `SYS_SELFTEST`, `SYS_IDLE`, `SYS_CONFIG`, `SYS_RUNNING`, `SYS_ERROR`, `SYS_SHUTDOWN`).
* Incorporación de interrupción por hardware para parada de emergencia (`E-STOP`) con corte directo de la línea `ENA` (activa en nivel bajo) del driver **DM856**.
* Pruebas de interfaz de usuario con botonera, encoder rotatorio y señalización acústica con buzzer antes del arranque.

### **`v3.1.04` — (Octubre 2026) — Integración de Multiplexor I²C TCA9548A y Red de Acelerómetros**
* Migración de lectura de un único sensor a topología multiplexada mediante **TCA9548A** (`0x70`) sobre bus I²C a $400\text{ kHz}$.
* Implementación de rutina de compensación de *bias/offset* inicial con mesa en reposo y configuración del filtro digital interno (`DLPF = 44 Hz`) de los **MPU6050** para mitigar el ruido mecánico del husillo.

### **`v2.8.40` — (Septiembre 2026) — Metrología Mecánica y Recalibración de Husillo**
* Corrección de parámetros geométricos tras pruebas de desplazamiento en banco ($8\,000\text{ pasos} \rightarrow 5.958\text{ mm}$ en un sentido; $11.916\text{ mm}$ ida y vuelta).
* Descarte de la hipótesis inicial de husillo de $8\text{ mm}$ de paso, ajustando la constante cinemática a las mediciones físicas ($\approx 4.82\text{ mm}$ de avance y relación efectiva de $1342.1\text{ pasos/mm}$ con microstepping $1/16$).
* Estandarización del pinout principal para la etapa de potencia (`STEP = GPIO 25`, `DIR = GPIO 26`, `ENA = GPIO 27`), separándolo de los pines de ensayo en banco (`12/14/27`).

### **`v2.5.12` — (Agosto 2026) — Protocolo de Comunicaciones `<CMD:VAL>` y Pruebas de Temporización**
* Diseño del protocolo serie estructurado con delimitadores `<START>`, `<STOP>`, `<SET:FREQ>` y `<SET:AMP>` sobre `UART0` a $115\,200\text{ bps}$.
* Identificación y diagnóstico de microcortes (*stutters*) y disparos del *Task Watchdog Timer* causados por retardos bloqueantes (`esp_rom_delay_us`) en la tarea del motor, iniciando la transición hacia temporizadores de hardware.

### **`v2.2.05` — (Julio 2026) — Incorporación de Pantalla TFT ST7789 y Colas de FreeRTOS**
* Integración del driver `esp_lcd` sobre el bus `SPI2_HOST` para el panel **ST7789** y validación mediante secuencias de prueba RGB565.
* Creación de las colas globales de intercomunicación (`cola_sensores` y `cola_uart`) para desacoplar las tareas del sistema operativo.

### **`v1.9.2` — (Junio 2026) — Línea Base del Proyecto (`OS_FreeRTOS`)**
* Creación del proyecto base en **PlatformIO + ESP-IDF** para la placa `esp32doit-devkit-v1`.
* Estructura inicial de cuatro tareas concurrentes (`motor_task`, `uart_task`, `sensor_task`, `ui_task`), pruebas preliminares de giro en pines `GPIO 21 / 19` con datos de aceleración simulados y plantilla de utilidades UART.

---

## 5. Compilación y Carga con PlatformIO (VS Code)

1. Abrir la carpeta `Firmware_CST` en **Visual Studio Code** con la extensión **PlatformIO IDE**.
2. Para compilar el proyecto:
   * Usar el botón **Build (`✓`)** en la barra inferior de PlatformIO, o ejecutar:
     ```powershell
     pio run
     ```
3. Para cargar el firmware al **ESP32 DOIT DevKit V1**:
   * Conectar la placa por USB y presionar **Upload (`→`)**, o ejecutar:
     ```powershell
     pio run --target upload
     ```
4. Para monitorear el arranque y el diagnóstico I²C:
   ```powershell
   pio device monitor -b 115200
   ```

---

## Licencia y Autoría
* **Autor:** Ing. Brayan Pantoja (`BrazanXL`) — Ingeniería Electrónica, Universidad de la Costa (CUC).
* **Licencia:** MIT License.
