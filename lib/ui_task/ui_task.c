#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"

#define PIN_LCD_SCLK 18   // SCL
#define PIN_LCD_MOSI 23   // SDA
#define PIN_LCD_CS   5    // CS
#define PIN_LCD_DC   16   // DC
#define PIN_LCD_RST  17   // RES
#define PIN_LCD_BL   4    // BLK (o conecta directo a 3V3)

#define LCD_H_RES 240
#define LCD_V_RES 240

static const char *TAG = "UI_TEST";

static esp_lcd_panel_handle_t panel = NULL;

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void ui_task(void *arg) {
    ESP_LOGI(TAG, "Iniciando pantalla...");

    // Configura bus SPI
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_LCD_SCLK,
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * 80 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // IO de panel
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &io));

    // Panel ST7789
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_endian = LCD_RGB_ENDIAN_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    // Activa backlight
    gpio_reset_pin(PIN_LCD_BL);
    gpio_set_direction(PIN_LCD_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_LCD_BL, 1);

    ESP_LOGI(TAG, "Prueba de colores...");
    static uint16_t buf[LCD_H_RES * LCD_V_RES];

    // Pantalla ROJA
    for (int i = 0; i < LCD_H_RES * LCD_V_RES; i++) buf[i] = rgb565(255,0,0);
    esp_lcd_panel_draw_bitmap(panel, 0, 0, LCD_H_RES, LCD_V_RES, buf);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Pantalla VERDE
    for (int i = 0; i < LCD_H_RES * LCD_V_RES; i++) buf[i] = rgb565(0,255,0);
    esp_lcd_panel_draw_bitmap(panel, 0, 0, LCD_H_RES, LCD_V_RES, buf);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Pantalla AZUL
    for (int i = 0; i < LCD_H_RES * LCD_V_RES; i++) buf[i] = rgb565(0,0,255);
    esp_lcd_panel_draw_bitmap(panel, 0, 0, LCD_H_RES, LCD_V_RES, buf);
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "Prueba terminada, quedará en negro");
    for (int i = 0; i < LCD_H_RES * LCD_V_RES; i++) buf[i] = rgb565(0,0,0);
    esp_lcd_panel_draw_bitmap(panel, 0, 0, LCD_H_RES, LCD_V_RES, buf);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
