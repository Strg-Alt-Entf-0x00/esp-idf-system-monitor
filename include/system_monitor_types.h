#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYS_MON_MAX_CORES 32
#define SYS_MON_HISTORY_SAMPLES 60
/* Kept for source compatibility; the history length is a sample count. */
#define SYS_MON_HISTORY_SECS SYS_MON_HISTORY_SAMPLES
#define SYS_MON_TIMEOUT_MS 5000
#define SYS_MON_ACK_SNOOZE_MS 90000
#define SYS_MON_FAN_RPM_UNAVAILABLE UINT16_MAX

typedef enum {
    SYS_MON_METRIC_CPU_TEMP = 0,
    SYS_MON_METRIC_GPU_TEMP,
    SYS_MON_METRIC_CPU_LOAD,
    SYS_MON_METRIC_GPU_LOAD,
    SYS_MON_METRIC_RAM,
    SYS_MON_METRIC_VRAM,
    SYS_MON_METRIC_SSD_TEMP,
    SYS_MON_METRIC_FAN_CPU,
    SYS_MON_METRIC_NETWORK,
    SYS_MON_METRIC_CORES,
    SYS_MON_METRIC_COUNT
} system_monitor_metric_t;

typedef enum {
    SYS_MON_ALARM_NONE        = 0,
    SYS_MON_ALARM_CPU_TEMP    = (1 << 0),
    SYS_MON_ALARM_GPU_TEMP    = (1 << 1),
    SYS_MON_ALARM_RAM         = (1 << 2),
    SYS_MON_ALARM_FAN_CPU     = (1 << 3),
    SYS_MON_ALARM_PC_OFFLINE  = (1 << 4)
} system_monitor_alarm_flags_t;

typedef struct {
    uint32_t sequence;
    float    cpu_temp;
    float    gpu_temp;
    float    ssd_temp;
    float    cpu_load;
    float    gpu_load;
    float    ram_used_gb;
    float    ram_total_gb;
    float    vram_used_gb;
    float    vram_total_gb;
    uint16_t fan_cpu;
    uint16_t fan_case;
    uint16_t fan_gpu;
    float    cpu_power_w;
    float    gpu_power_w;
    float    net_down_mbps;
    float    net_up_mbps;
    uint8_t  core_count;
    float    core_loads[SYS_MON_MAX_CORES];
    int64_t  timestamp_ms;
} system_monitor_telemetry_t;

typedef struct {
    char cpu_name[64];
    char gpu_name[64];
    char ram_name[64];
    char ssd_name[64];
    char net_name[64];
} system_monitor_info_t;

typedef struct {
    float cpu_temp_peak;
    float gpu_temp_peak;
    float ssd_temp_peak;
    float cpu_load_peak;
    float gpu_load_peak;
} system_monitor_peaks_t;

typedef struct {
    float    cpu_temp_crit;     // Default: 85.0f
    float    gpu_temp_crit;     // Default: 85.0f
    float    ram_pct_crit;      // Default: 95.0f
    uint16_t fan_stall_min_rpm; // Default: 200; alarms below this at CPU load >= 20%
} system_monitor_thresholds_t;

typedef struct {
    system_monitor_thresholds_t thresholds;
    uint32_t auto_cycle_interval_ms; // 0 to disable auto cycle, e.g. 4000ms
} system_monitor_config_t;

#ifdef __cplusplus
}
#endif
