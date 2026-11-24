#include "display_hal.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_gc9a01.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "sys_mon_hal";

#define LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)
#define LCD_DRAW_ROWS 8

namespace {
class LGFX_Monitor_Dummy : public lgfx::LGFX_Device {
public:
    LGFX_Monitor_Dummy(void) {}
};

static LGFX_Monitor_Dummy s_lcd;
static lgfx::LGFX_Sprite s_sprite(&s_lcd);
static esp_lcd_panel_handle_t s_panel_handle = NULL;
static esp_lcd_panel_io_handle_t s_panel_io_handle = NULL;
static spi_host_device_t s_spi_host;
static bool s_spi_bus_owned = false;
static int s_backlight_pin = -1;
static bool s_backlight_initialized = false;
static int s_width = LCD_H_RES;
static int s_height = LCD_V_RES;
}

static esp_err_t init_backlight(int pin) {
    if (pin < 0) {
        s_backlight_pin = -1;
        return ESP_OK;
    }
    s_backlight_pin = pin;

    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.timer_num = LEDC_TIMER_0;
    timer.duty_resolution = LEDC_TIMER_8_BIT;
    timer.freq_hz = 5000;
    timer.clk_cfg = LEDC_AUTO_CLK;
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) return err;

    ledc_channel_config_t channel = {};
    channel.speed_mode = LEDC_LOW_SPEED_MODE;
    channel.channel = LEDC_CHANNEL_0;
    channel.timer_sel = LEDC_TIMER_0;
    channel.gpio_num = pin;
    channel.duty = 0;
    channel.hpoint = 0;
    err = ledc_channel_config(&channel);
    if (err != ESP_OK) return err;

    for (int duty = 0; duty <= 255; duty += 15) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    s_backlight_initialized = true;
    return ESP_OK;
}

esp_err_t sys_mon_hal_init(const system_monitor_display_config_t* config) {
    if (!config) {
        ESP_LOGE(TAG, "Config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing GC9A01 240x240 circular display (SPI host %d)", config->spi_host);

    // 1. SPI Bus Initialization
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = config->pin_mosi;
    buscfg.miso_io_num = -1;
    buscfg.sclk_io_num = config->pin_sclk;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = LCD_H_RES * LCD_DRAW_ROWS * sizeof(uint16_t);

    esp_err_t ret = spi_bus_initialize(config->spi_host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_spi_host = config->spi_host;
    s_spi_bus_owned = (ret == ESP_OK);

    // 2. Install Panel IO
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = (gpio_num_t)config->pin_cs;
    io_config.dc_gpio_num = (gpio_num_t)config->pin_dc;
    io_config.spi_mode = 0;
    io_config.pclk_hz = config->spi_clock_hz > 0 ? config->spi_clock_hz : LCD_PIXEL_CLOCK_HZ;
    io_config.trans_queue_depth = 2;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;

    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)config->spi_host,
                                   &io_config, &s_panel_io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_spi failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 3. Install GC9A01 Panel Driver
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    panel_config.bits_per_pixel = 16;
    panel_config.reset_gpio_num = (gpio_num_t)config->pin_rst;

    ret = esp_lcd_new_panel_gc9a01(s_panel_io_handle, &panel_config, &s_panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_gc9a01 failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_lcd_panel_reset(s_panel_handle);
    if (ret == ESP_OK) ret = esp_lcd_panel_init(s_panel_handle);
    if (ret == ESP_OK) ret = esp_lcd_panel_invert_color(s_panel_handle, true);
    if (ret == ESP_OK) ret = esp_lcd_panel_mirror(s_panel_handle, true, false);
    if (ret == ESP_OK) ret = esp_lcd_panel_disp_on_off(s_panel_handle, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GC9A01 hardware initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 4. Initialize LovyanGFX PSRAM Sprite Buffer
    s_width = (config->width > 0) ? config->width : LCD_H_RES;
    s_height = (config->height > 0) ? config->height : LCD_V_RES;

    s_lcd.init();
    s_lcd.setRotation(0);
    s_lcd.setBrightness(255);

    s_sprite.setColorDepth(16);
    s_sprite.setPsram(true);
    s_sprite.createSprite(s_width, s_height);
    if (!s_sprite.getBuffer()) {
        ESP_LOGW(TAG, "PSRAM sprite allocation failed, falling back to internal RAM");
        s_sprite.setPsram(false);
        s_sprite.createSprite(s_width, s_height);
    }
    if (!s_sprite.getBuffer()) {
        ESP_LOGE(TAG, "Failed to allocate %dx%d sprite", s_width, s_height);
        return ESP_ERR_NO_MEM;
    }
    s_sprite.setTextDatum(lgfx::middle_center);
    s_sprite.setSwapBytes(true);

    // 5. Backlight
    init_backlight(config->pin_bkl);

    ESP_LOGI(TAG, "GC9A01 Display HAL ready (%dx%d double buffered in PSRAM)", s_width, s_height);
    return ESP_OK;
}

void sys_mon_hal_push_frame(void) {
    if (!s_panel_handle || !s_sprite.getBuffer()) return;

    const auto* pixels = static_cast<const uint16_t*>(s_sprite.getBuffer());
    for (int y = 0; y < s_height; y += LCD_DRAW_ROWS) {
        int rows = ((y + LCD_DRAW_ROWS) <= s_height) ? LCD_DRAW_ROWS : (s_height - y);
        const esp_err_t ret = esp_lcd_panel_draw_bitmap(
            s_panel_handle, 0, y, s_width, y + rows,
            pixels + y * s_width);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Draw bitmap failed at y=%d: %s", y, esp_err_to_name(ret));
            return;
        }
    }
}

void sys_mon_hal_set_brightness(uint8_t brightness) {
    if (s_backlight_pin < 0 || !s_backlight_initialized) return;
    if (brightness > 100) brightness = 100;
    uint32_t duty = (255 * brightness) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

lgfx::LGFX_Sprite* sys_mon_hal_get_sprite(void) {
    return &s_sprite;
}

esp_err_t sys_mon_hal_deinit(void) {
    s_sprite.deleteSprite();
    if (s_panel_handle) {
        esp_lcd_panel_disp_on_off(s_panel_handle, false);
        esp_lcd_panel_del(s_panel_handle);
        s_panel_handle = NULL;
    }
    if (s_panel_io_handle) {
        esp_lcd_panel_io_del(s_panel_io_handle);
        s_panel_io_handle = NULL;
    }
    if (s_spi_bus_owned) {
        spi_bus_free(s_spi_host);
        s_spi_bus_owned = false;
    }
    return ESP_OK;
}
