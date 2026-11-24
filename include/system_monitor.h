#pragma once

#include "system_monitor_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize system monitor state and peak tracking.
 *
 * This component may be initialized once per boot. A repeated call returns
 * ESP_ERR_INVALID_STATE.
 */
esp_err_t system_monitor_init(const system_monitor_config_t *config);

/**
 * @brief Read or update alarm thresholds and auto-cycle interval at runtime.
 *
 * CPU/GPU temperature thresholds must be 0..200 C, RAM must be greater than
 * 0 and at most 100 percent, fan RPM must be 0..20000, and the cycle interval
 * must be 0..3600000 ms. A zero fan threshold disables that alarm; a zero cycle
 * interval disables automatic cycling.
 */
esp_err_t system_monitor_get_config(system_monitor_config_t *out_config);
esp_err_t system_monitor_set_config(const system_monitor_config_t *config);

/**
 * @brief Parse one incoming telemetry line (starts with !pc_hw or !pc_hw_info).
 *
 * The transport is managed by the integrating application; despite the
 * function's historical name, the parser itself is not tied to serial.
 */
esp_err_t system_monitor_feed_serial_line(const char *line);

/**
 * @brief Periodic tick function to update timers, watchdog, and peaks.
 * @param elapsed_ms Milliseconds elapsed since last call.
 */
void system_monitor_tick(uint32_t elapsed_ms);

/**
 * @brief Check if PC communication is active (received packet within timeout).
 */
bool system_monitor_is_connected(void);

/**
 * @brief Get latest telemetry data.
 * @return ESP_ERR_INVALID_ARG for a null output pointer or
 *         ESP_ERR_INVALID_STATE before initialization.
 */
esp_err_t system_monitor_get_telemetry(system_monitor_telemetry_t *out_telem);

/**
 * @brief Get hardware names info.
 * @return ESP_ERR_INVALID_ARG for a null output pointer or
 *         ESP_ERR_INVALID_STATE before initialization.
 */
esp_err_t system_monitor_get_info(system_monitor_info_t *out_info);

/**
 * @brief Get rolling peak values over the most recent 60 telemetry samples.
 *
 * A field is NAN when no valid reading for that metric exists in the window.
 * @return ESP_ERR_INVALID_ARG for a null output pointer or
 *         ESP_ERR_INVALID_STATE before initialization.
 */
esp_err_t system_monitor_get_peaks(system_monitor_peaks_t *out_peaks);

/**
 * @brief Get active alarms and warnings bitmask.
 * @param out_warnings Optional pointer to receive warning flags (10°C / 10% below crit).
 * @return Bitmask of active critical alarms.
 */
uint32_t system_monitor_get_alarms(uint32_t *out_warnings);

/**
 * @brief Acknowledge/snooze alarms for 90 seconds.
 */
void system_monitor_acknowledge_alarm(void);

/**
 * @brief Handle rotary encoder rotation.
 * @param direction +1 for clockwise, -1 for counter-clockwise.
 * @param pressed True if button was held during turn.
 */
void system_monitor_on_encoder_rotate(int8_t direction, bool pressed);

/**
 * @brief Handle encoder button click.
 * @param long_press True if held > 1 second.
 */
void system_monitor_on_encoder_click(bool long_press);

/**
 * @brief Explicitly hold or release automatic metric cycling.
 */
void system_monitor_set_cycle_held(bool held);

/**
 * @brief Get the currently active metric displayed in the center.
 */
system_monitor_metric_t system_monitor_get_center_metric(void);

/**
 * @brief Explicitly set the active center metric (e.g. from voice command).
 */
void system_monitor_set_center_metric(system_monitor_metric_t metric);

/**
 * @brief Check if display auto-cycle is currently held/frozen.
 */
bool system_monitor_is_cycle_held(void);

#ifdef __cplusplus
}
#endif
