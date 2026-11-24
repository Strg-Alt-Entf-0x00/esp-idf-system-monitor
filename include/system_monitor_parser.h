#pragma once

#include "system_monitor_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parses a "!pc_hw ..." serial packet into telemetry struct.
 * @param line Null-terminated packet string.
 * @param out_data Output telemetry struct.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on parse failure.
 */
esp_err_t system_monitor_parse_hw_line(const char *line, system_monitor_telemetry_t *out_data);

/**
 * @brief Parses a "!pc_hw_info ..." serial packet into info struct.
 * @param line Null-terminated packet string.
 * @param out_info Output info struct.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on parse failure.
 */
esp_err_t system_monitor_parse_info_line(const char *line, system_monitor_info_t *out_info);

#ifdef __cplusplus
}
#endif

