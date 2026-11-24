#pragma once

#include "system_monitor_display.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#define LCD_H_RES 240
#define LCD_V_RES 240

esp_err_t sys_mon_hal_init(const system_monitor_display_config_t* config);
esp_err_t sys_mon_hal_deinit(void);
void sys_mon_hal_push_frame(void);
void sys_mon_hal_set_brightness(uint8_t brightness);
lgfx::LGFX_Sprite* sys_mon_hal_get_sprite(void);
