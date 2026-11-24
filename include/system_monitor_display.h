#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hal/spi_types.h"
#include "system_monitor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SYS_MON_LANG_DE = 0,
    SYS_MON_LANG_EN,
} system_monitor_lang_t;

typedef enum {
    SYS_MON_THEME_MONO = 0,    // High-contrast pure black & white, amber/red alerts
    SYS_MON_THEME_CYAN,        // Cyber cyan accent
    SYS_MON_THEME_AMBER,       // Classic amber / industrial theme
} system_monitor_theme_t;

typedef struct {
    int pin_mosi;       // Default: 2 (ESP32-P4 GC9A01 SDA)
    int pin_sclk;       // Default: 3 (ESP32-P4 GC9A01 SCL)
    int pin_cs;         // Default: 4 (ESP32-P4 GC9A01 CS)
    int pin_dc;         // Default: 5 (ESP32-P4 GC9A01 DC)
    int pin_rst;        // Default: 49 (ESP32-P4 GC9A01 RST)
    int pin_bkl;        // Default: -1 (tied to 3.3V or LEDC pin)
    spi_host_device_t spi_host; // Default: SPI2_HOST
    uint32_t spi_clock_hz;      // Default: 40000000 (40 MHz)
    uint16_t width;             // Default: 240 (reference resolution)
    uint16_t height;            // Default: 240
    system_monitor_lang_t language;
    system_monitor_theme_t theme;
} system_monitor_display_config_t;

/**
 * @brief Get default display hardware configuration for Waveshare ESP32-P4 GC9A01
 */
system_monitor_display_config_t system_monitor_display_default_config(void);

/**
 * @brief Get current display scaling factor relative to reference 240x240
 */
float system_monitor_display_get_scale(void);

/**
 * @brief Dynamically set display geometry for resolution scaling (e.g. 240, 280, 360, 480)
 */
void system_monitor_display_set_geometry(int width, int height);

/**
 * @brief Initialize GC9A01 circular display and allocate PSRAM sprite buffer
 */
esp_err_t system_monitor_display_init(const system_monitor_display_config_t *config);

/**
 * @brief Render one frame to the PSRAM buffer and flush to GC9A01
 * Called periodically (e.g. at 30 FPS) from a display task.
 */
void system_monitor_display_render(void);

/**
 * @brief Set display backlight brightness (0..100%)
 */
void system_monitor_display_set_brightness(uint8_t brightness);

/**
 * @brief Set active display language (DE / EN)
 */
void system_monitor_display_set_language(system_monitor_lang_t lang);

/**
 * @brief Set active color theme
 */
void system_monitor_display_set_theme(system_monitor_theme_t theme);

/**
 * @brief Deinitialize display and release resources
 */
esp_err_t system_monitor_display_deinit(void);

#ifdef __cplusplus
}
#endif
