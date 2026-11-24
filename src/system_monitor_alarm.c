#include "system_monitor_types.h"

#include <math.h>
#include <string.h>

#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "sys_mon_alarm";

#define PEAK_HISTORY_POINTS SYS_MON_HISTORY_SAMPLES

typedef struct {
    float cpu_temp;
    float gpu_temp;
    float ssd_temp;
    float cpu_load;
    float gpu_load;
} history_entry_t;

static history_entry_t s_history[PEAK_HISTORY_POINTS];
static size_t s_history_head = 0;
static size_t s_history_count = 0;

static system_monitor_thresholds_t s_thresholds = {
    .cpu_temp_crit = 85.0f,
    .gpu_temp_crit = 85.0f,
    .ram_pct_crit = 95.0f,
    .fan_stall_min_rpm = 200
};

static int64_t s_alarm_snooze_until_ms = 0;

void system_monitor_alarm_init(const system_monitor_thresholds_t *thresh)
{
    if (thresh) {
        s_thresholds = *thresh;
    }
    memset(s_history, 0, sizeof(s_history));
    s_history_head = 0;
    s_history_count = 0;
    s_alarm_snooze_until_ms = 0;
}

void system_monitor_alarm_set_thresholds(const system_monitor_thresholds_t *thresh)
{
    if (thresh) {
        s_thresholds = *thresh;
    }
}

void system_monitor_alarm_feed_telemetry(const system_monitor_telemetry_t *t)
{
    if (!t) return;

    s_history[s_history_head].cpu_temp = t->cpu_temp;
    s_history[s_history_head].gpu_temp = t->gpu_temp;
    s_history[s_history_head].ssd_temp = t->ssd_temp;
    s_history[s_history_head].cpu_load = t->cpu_load;
    s_history[s_history_head].gpu_load = t->gpu_load;

    s_history_head = (s_history_head + 1) % PEAK_HISTORY_POINTS;
    if (s_history_count < PEAK_HISTORY_POINTS) {
        s_history_count++;
    }
}

void system_monitor_alarm_get_peaks(system_monitor_peaks_t *out_peaks)
{
    if (!out_peaks) return;
    memset(out_peaks, 0, sizeof(system_monitor_peaks_t));
    bool has_cpu_temp = false;
    bool has_gpu_temp = false;
    bool has_ssd_temp = false;
    bool has_cpu_load = false;
    bool has_gpu_load = false;

    for (size_t i = 0; i < s_history_count; ++i) {
        const history_entry_t *sample = &s_history[i];
        if (isfinite(sample->cpu_temp) &&
            (!has_cpu_temp || sample->cpu_temp > out_peaks->cpu_temp_peak)) {
            out_peaks->cpu_temp_peak = sample->cpu_temp;
            has_cpu_temp = true;
        }
        if (isfinite(sample->gpu_temp) &&
            (!has_gpu_temp || sample->gpu_temp > out_peaks->gpu_temp_peak)) {
            out_peaks->gpu_temp_peak = sample->gpu_temp;
            has_gpu_temp = true;
        }
        if (isfinite(sample->ssd_temp) &&
            (!has_ssd_temp || sample->ssd_temp > out_peaks->ssd_temp_peak)) {
            out_peaks->ssd_temp_peak = sample->ssd_temp;
            has_ssd_temp = true;
        }
        if (isfinite(sample->cpu_load) &&
            (!has_cpu_load || sample->cpu_load > out_peaks->cpu_load_peak)) {
            out_peaks->cpu_load_peak = sample->cpu_load;
            has_cpu_load = true;
        }
        if (isfinite(sample->gpu_load) &&
            (!has_gpu_load || sample->gpu_load > out_peaks->gpu_load_peak)) {
            out_peaks->gpu_load_peak = sample->gpu_load;
            has_gpu_load = true;
        }
    }

    if (!has_cpu_temp) out_peaks->cpu_temp_peak = NAN;
    if (!has_gpu_temp) out_peaks->gpu_temp_peak = NAN;
    if (!has_ssd_temp) out_peaks->ssd_temp_peak = NAN;
    if (!has_cpu_load) out_peaks->cpu_load_peak = NAN;
    if (!has_gpu_load) out_peaks->gpu_load_peak = NAN;
}

uint32_t system_monitor_alarm_evaluate(const system_monitor_telemetry_t *t, bool is_connected, uint32_t *out_warnings)
{
    uint32_t crit = SYS_MON_ALARM_NONE;
    uint32_t warn = SYS_MON_ALARM_NONE;

    if (!is_connected) {
        crit |= SYS_MON_ALARM_PC_OFFLINE;
        if (out_warnings) *out_warnings = 0;
        return crit;
    }

    if (!t) {
        if (out_warnings) *out_warnings = 0;
        return crit;
    }

    // Check CPU temp
    if (t->cpu_temp >= s_thresholds.cpu_temp_crit) {
        crit |= SYS_MON_ALARM_CPU_TEMP;
    } else if (t->cpu_temp >= (s_thresholds.cpu_temp_crit - 10.0f)) {
        warn |= SYS_MON_ALARM_CPU_TEMP;
    }

    // Check GPU temp
    if (t->gpu_temp >= s_thresholds.gpu_temp_crit) {
        crit |= SYS_MON_ALARM_GPU_TEMP;
    } else if (t->gpu_temp >= (s_thresholds.gpu_temp_crit - 10.0f)) {
        warn |= SYS_MON_ALARM_GPU_TEMP;
    }

    // Check RAM load %
    float ram_pct = (t->ram_total_gb > 0.0f) ? (t->ram_used_gb / t->ram_total_gb * 100.0f) : 0.0f;
    if (ram_pct >= s_thresholds.ram_pct_crit) {
        crit |= SYS_MON_ALARM_RAM;
    } else if (ram_pct >= (s_thresholds.ram_pct_crit - 10.0f)) {
        warn |= SYS_MON_ALARM_RAM;
    }

    // Missing RPM is not a stall; zero threshold disables the comparison.
    if (t->fan_cpu != SYS_MON_FAN_RPM_UNAVAILABLE &&
        t->cpu_load >= 20.0f &&
        t->fan_cpu < s_thresholds.fan_stall_min_rpm) {
        crit |= SYS_MON_ALARM_FAN_CPU;
    }

    // Handle alarm snooze / acknowledge
    int64_t now_ms = esp_timer_get_time() / 1000;
    if (now_ms < s_alarm_snooze_until_ms) {
        if (out_warnings) *out_warnings = 0;
        return SYS_MON_ALARM_NONE;
    }

    if (out_warnings) *out_warnings = warn;
    return crit;
}

void system_monitor_alarm_snooze(void)
{
    s_alarm_snooze_until_ms = (esp_timer_get_time() / 1000) + SYS_MON_ACK_SNOOZE_MS;
    ESP_LOGI(TAG, "Alarms acknowledged / snoozed for %d seconds", SYS_MON_ACK_SNOOZE_MS / 1000);
}
