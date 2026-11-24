#include "system_monitor.h"
#include "system_monitor_parser.h"

#include <math.h>
#include <string.h>

#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "sys_mon";

// Internal alarm helpers from system_monitor_alarm.c
extern void system_monitor_alarm_init(const system_monitor_thresholds_t *thresh);
extern void system_monitor_alarm_set_thresholds(const system_monitor_thresholds_t *thresh);
extern void system_monitor_alarm_feed_telemetry(const system_monitor_telemetry_t *t);
extern void system_monitor_alarm_get_peaks(system_monitor_peaks_t *out_peaks);
extern uint32_t system_monitor_alarm_evaluate(const system_monitor_telemetry_t *t, bool is_connected, uint32_t *out_warnings);
extern void system_monitor_alarm_snooze(void);

typedef struct {
    system_monitor_config_t     config;
    system_monitor_telemetry_t  latest_telem;
    system_monitor_info_t       latest_info;
    int64_t                     last_packet_time_ms;
    bool                        connected;
    system_monitor_metric_t     center_metric;
    bool                        cycle_held;
    uint32_t                    cycle_timer_ms;
    SemaphoreHandle_t           mutex;
    bool                        initialized;
} sys_mon_ctx_t;

static sys_mon_ctx_t s_ctx;

static bool is_valid_config(const system_monitor_config_t *config)
{
    return config &&
           isfinite(config->thresholds.cpu_temp_crit) &&
           config->thresholds.cpu_temp_crit >= 0.0f &&
           config->thresholds.cpu_temp_crit <= 200.0f &&
           isfinite(config->thresholds.gpu_temp_crit) &&
           config->thresholds.gpu_temp_crit >= 0.0f &&
           config->thresholds.gpu_temp_crit <= 200.0f &&
           isfinite(config->thresholds.ram_pct_crit) &&
           config->thresholds.ram_pct_crit > 0.0f &&
           config->thresholds.ram_pct_crit <= 100.0f &&
           config->thresholds.fan_stall_min_rpm <= 20000 &&
           config->auto_cycle_interval_ms <= 3600000;
}

esp_err_t system_monitor_init(const system_monitor_config_t *config)
{
    if (s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    sys_mon_ctx_t initial = {0};
    initial.config.thresholds.cpu_temp_crit = 85.0f;
    initial.config.thresholds.gpu_temp_crit = 85.0f;
    initial.config.thresholds.ram_pct_crit = 95.0f;
    initial.config.thresholds.fan_stall_min_rpm = 200;
    initial.config.auto_cycle_interval_ms = 4000;
    if (config) {
        initial.config = *config;
    }
    if (!is_valid_config(&initial.config)) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.config = initial.config;

    s_ctx.mutex = xSemaphoreCreateMutex();
    if (!s_ctx.mutex) {
        return ESP_ERR_NO_MEM;
    }

    s_ctx.center_metric = SYS_MON_METRIC_CPU_TEMP;
    s_ctx.cycle_held = false;
    s_ctx.cycle_timer_ms = 0;
    s_ctx.connected = false;

    system_monitor_alarm_init(&s_ctx.config.thresholds);

    s_ctx.initialized = true;
    ESP_LOGI(TAG, "System Monitor initialized (auto-cycle=%lu ms)", 
             (unsigned long)s_ctx.config.auto_cycle_interval_ms);
    return ESP_OK;
}

esp_err_t system_monitor_get_config(system_monitor_config_t *out_config)
{
    if (!out_config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    *out_config = s_ctx.config;
    xSemaphoreGive(s_ctx.mutex);
    return ESP_OK;
}

esp_err_t system_monitor_set_config(const system_monitor_config_t *config)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!is_valid_config(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    s_ctx.config = *config;
    s_ctx.cycle_timer_ms = 0;
    system_monitor_alarm_set_thresholds(&config->thresholds);
    xSemaphoreGive(s_ctx.mutex);
    return ESP_OK;
}

esp_err_t system_monitor_feed_serial_line(const char *line)
{
    if (!line) return ESP_ERR_INVALID_ARG;
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (strncmp(line, "!pc_hw_info", 11) == 0) {
        system_monitor_info_t info;
        esp_err_t err = system_monitor_parse_info_line(line, &info);
        if (err == ESP_OK) {
            xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
            s_ctx.latest_info = info;
            xSemaphoreGive(s_ctx.mutex);
            ESP_LOGI(TAG, "Updated PC hardware info: CPU='%s', GPU='%s'", info.cpu_name, info.gpu_name);
        }
        return err;
    }

    if (strncmp(line, "!pc_hw", 6) == 0) {
        system_monitor_telemetry_t telem;
        esp_err_t err = system_monitor_parse_hw_line(line, &telem);
        if (err == ESP_OK) {
            int64_t now_ms = esp_timer_get_time() / 1000;
            xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
            s_ctx.latest_telem = telem;
            s_ctx.last_packet_time_ms = now_ms;
            s_ctx.connected = true;

            system_monitor_alarm_feed_telemetry(&telem);

            // Auto-jump to alarm metric if alarm triggered
            uint32_t crit = system_monitor_alarm_evaluate(&telem, true, NULL);
            if (crit != SYS_MON_ALARM_NONE) {
                if (crit & SYS_MON_ALARM_CPU_TEMP) s_ctx.center_metric = SYS_MON_METRIC_CPU_TEMP;
                else if (crit & SYS_MON_ALARM_GPU_TEMP) s_ctx.center_metric = SYS_MON_METRIC_GPU_TEMP;
                else if (crit & SYS_MON_ALARM_RAM) s_ctx.center_metric = SYS_MON_METRIC_RAM;
                else if (crit & SYS_MON_ALARM_FAN_CPU) s_ctx.center_metric = SYS_MON_METRIC_FAN_CPU;
            }
            xSemaphoreGive(s_ctx.mutex);
        }
        return err;
    }

    return ESP_ERR_NOT_SUPPORTED;
}

void system_monitor_tick(uint32_t elapsed_ms)
{
    if (!s_ctx.initialized) return;

    int64_t now_ms = esp_timer_get_time() / 1000;

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    if (s_ctx.connected && (now_ms - s_ctx.last_packet_time_ms > SYS_MON_TIMEOUT_MS)) {
        s_ctx.connected = false;
        ESP_LOGW(TAG, "PC connection timeout (> %d ms)", SYS_MON_TIMEOUT_MS);
    }

    const uint32_t cycle_interval_ms = s_ctx.config.auto_cycle_interval_ms;
    if (cycle_interval_ms > 0 && !s_ctx.cycle_held && s_ctx.connected) {
        if (elapsed_ms >= cycle_interval_ms - s_ctx.cycle_timer_ms) {
            s_ctx.cycle_timer_ms = 0;
            s_ctx.center_metric = (system_monitor_metric_t)((s_ctx.center_metric + 1) % SYS_MON_METRIC_COUNT);
        } else {
            s_ctx.cycle_timer_ms += elapsed_ms;
        }
    }
    xSemaphoreGive(s_ctx.mutex);
}

bool system_monitor_is_connected(void)
{
    if (!s_ctx.initialized) return false;
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    bool connected = s_ctx.connected;
    xSemaphoreGive(s_ctx.mutex);
    return connected;
}

esp_err_t system_monitor_get_telemetry(system_monitor_telemetry_t *out_telem)
{
    if (!out_telem) return ESP_ERR_INVALID_ARG;
    if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    *out_telem = s_ctx.latest_telem;
    xSemaphoreGive(s_ctx.mutex);
    return ESP_OK;
}

esp_err_t system_monitor_get_info(system_monitor_info_t *out_info)
{
    if (!out_info) return ESP_ERR_INVALID_ARG;
    if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    *out_info = s_ctx.latest_info;
    xSemaphoreGive(s_ctx.mutex);
    return ESP_OK;
}

esp_err_t system_monitor_get_peaks(system_monitor_peaks_t *out_peaks)
{
    if (!out_peaks) return ESP_ERR_INVALID_ARG;
    if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    system_monitor_alarm_get_peaks(out_peaks);
    xSemaphoreGive(s_ctx.mutex);
    return ESP_OK;
}

uint32_t system_monitor_get_alarms(uint32_t *out_warnings)
{
    if (!s_ctx.initialized) {
        if (out_warnings) *out_warnings = 0;
        return SYS_MON_ALARM_PC_OFFLINE;
    }
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    uint32_t crit = system_monitor_alarm_evaluate(&s_ctx.latest_telem, s_ctx.connected, out_warnings);
    xSemaphoreGive(s_ctx.mutex);
    return crit;
}

void system_monitor_acknowledge_alarm(void)
{
    if (!s_ctx.initialized) return;
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    system_monitor_alarm_snooze();
    xSemaphoreGive(s_ctx.mutex);
}

void system_monitor_set_cycle_held(bool held)
{
    if (!s_ctx.initialized) return;
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    s_ctx.cycle_held = held;
    s_ctx.cycle_timer_ms = 0;
    xSemaphoreGive(s_ctx.mutex);
}

void system_monitor_on_encoder_rotate(int8_t direction, bool pressed)
{
    if (!s_ctx.initialized) return;

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    if (pressed) {
        // Change center metric directly and freeze auto-cycle
        s_ctx.cycle_held = true;
        int next = (int)s_ctx.center_metric + direction;
        if (next < 0) next = SYS_MON_METRIC_COUNT - 1;
        if (next >= SYS_MON_METRIC_COUNT) next = 0;
        s_ctx.center_metric = (system_monitor_metric_t)next;
        s_ctx.cycle_timer_ms = 0;
    } else {
        // Normal turn: change page/view or step through metrics
        int next = (int)s_ctx.center_metric + direction;
        if (next < 0) next = SYS_MON_METRIC_COUNT - 1;
        if (next >= SYS_MON_METRIC_COUNT) next = 0;
        s_ctx.center_metric = (system_monitor_metric_t)next;
    }
    xSemaphoreGive(s_ctx.mutex);
}

void system_monitor_on_encoder_click(bool long_press)
{
    if (!s_ctx.initialized) return;

    if (long_press) {
        // Long press -> standby or sleep (handled by app)
        ESP_LOGI(TAG, "Encoder long press: standby toggle");
    } else {
        // Short press: acknowledge active alarm, OR toggle auto-cycle freeze/hold
        uint32_t warnings = 0;
        const uint32_t crit = system_monitor_get_alarms(&warnings);
        const uint32_t actionable_crit = crit & ~SYS_MON_ALARM_PC_OFFLINE;
        if (actionable_crit != SYS_MON_ALARM_NONE || warnings != SYS_MON_ALARM_NONE) {
            system_monitor_acknowledge_alarm();
        } else {
            xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
            s_ctx.cycle_held = !s_ctx.cycle_held;
            s_ctx.cycle_timer_ms = 0;
            ESP_LOGI(TAG, "Display cycle hold toggled: %s", s_ctx.cycle_held ? "HELD" : "RUNNING");
            xSemaphoreGive(s_ctx.mutex);
        }
    }
}

system_monitor_metric_t system_monitor_get_center_metric(void)
{
    if (!s_ctx.initialized) return SYS_MON_METRIC_CPU_TEMP;
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    system_monitor_metric_t metric = s_ctx.center_metric;
    xSemaphoreGive(s_ctx.mutex);
    return metric;
}

void system_monitor_set_center_metric(system_monitor_metric_t metric)
{
    if (metric >= SYS_MON_METRIC_CPU_TEMP && metric < SYS_MON_METRIC_COUNT) {
        if (!s_ctx.initialized) return;
        xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
        s_ctx.center_metric = metric;
        xSemaphoreGive(s_ctx.mutex);
    }
}

bool system_monitor_is_cycle_held(void)
{
    if (!s_ctx.initialized) return false;
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    bool held = s_ctx.cycle_held;
    xSemaphoreGive(s_ctx.mutex);
    return held;
}
