#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "system_monitor.h"
#include "system_monitor_display.h"
#include "uart_utf8_console.h"

#define COMMAND_LINE_SIZE 512
#define COMMAND_QUEUE_LENGTH 32

static const char *TAG = "system_monitor_demo";
static QueueHandle_t s_command_queue;

typedef struct {
  char line[COMMAND_LINE_SIZE];
} console_command_t;

static const char *const s_metric_names[SYS_MON_METRIC_COUNT] = {
    "cpu-temp", "gpu-temp", "cpu-load", "gpu-load", "ram",
    "vram",     "ssd-temp", "fan-cpu",  "network",  "cores"};

static const char *metric_name(system_monitor_metric_t metric) {
  if (metric < SYS_MON_METRIC_CPU_TEMP || metric >= SYS_MON_METRIC_COUNT)
    return "unknown";
  return s_metric_names[metric];
}

static system_monitor_config_t default_config(void) {
  return (system_monitor_config_t){.thresholds = {.cpu_temp_crit = 85.0f,
                                                  .gpu_temp_crit = 85.0f,
                                                  .ram_pct_crit = 95.0f,
                                                  .fan_stall_min_rpm = 200},
                                   .auto_cycle_interval_ms = 4000};
}

static const char *alarm_name(uint32_t flag) {
  switch (flag) {
  case SYS_MON_ALARM_CPU_TEMP:
    return "CPU temperature";
  case SYS_MON_ALARM_GPU_TEMP:
    return "GPU temperature";
  case SYS_MON_ALARM_RAM:
    return "RAM usage";
  case SYS_MON_ALARM_FAN_CPU:
    return "CPU fan stall";
  case SYS_MON_ALARM_PC_OFFLINE:
    return "PC offline";
  default:
    return "none";
  }
}

static void print_alarm_flags(const char *label, uint32_t flags) {
  static const uint32_t bits[] = {
      SYS_MON_ALARM_CPU_TEMP, SYS_MON_ALARM_GPU_TEMP, SYS_MON_ALARM_RAM,
      SYS_MON_ALARM_FAN_CPU, SYS_MON_ALARM_PC_OFFLINE};
  bool found = false;
  printf("%s:", label);
  for (size_t i = 0; i < sizeof(bits) / sizeof(bits[0]); ++i) {
    if (flags & bits[i]) {
      printf(" %s%s", found ? "," : "", alarm_name(bits[i]));
      found = true;
    }
  }
  printf("%s\n", found ? "" : " none");
}

static void print_optional_float(float value, int precision) {
  if (isfinite(value))
    printf("%.*f", precision, (double)value);
  else
    printf("n/a");
}

static void print_telemetry(const system_monitor_telemetry_t *telemetry) {
  printf("Sequence: %" PRIu32 " | sample age: ", telemetry->sequence);
  if (telemetry->timestamp_ms > 0) {
    printf("%" PRId64 " ms\n",
           esp_timer_get_time() / 1000 - telemetry->timestamp_ms);
  } else {
    printf("no sample received\n");
    printf("Temperature C: CPU n/a | GPU n/a | SSD n/a\n");
    printf("Load %%: CPU n/a | GPU n/a\n");
    printf("Memory GB: RAM n/a | VRAM n/a\n");
    printf("Fans RPM: CPU n/a | case n/a | GPU n/a\n");
    printf("Power W: CPU n/a | GPU n/a\n");
    printf("Network Mbps: down n/a | up n/a\n");
    printf("Per-core CPU load: not reported\n");
    return;
  }
  printf("Temperature C: CPU ");
  print_optional_float(telemetry->cpu_temp, 1);
  printf(" | GPU ");
  print_optional_float(telemetry->gpu_temp, 1);
  printf(" | SSD ");
  print_optional_float(telemetry->ssd_temp, 1);
  printf("\nLoad %%: CPU ");
  print_optional_float(telemetry->cpu_load, 1);
  printf(" | GPU ");
  print_optional_float(telemetry->gpu_load, 1);
  printf("\nMemory GB: RAM ");
  print_optional_float(telemetry->ram_used_gb, 2);
  printf("/");
  print_optional_float(telemetry->ram_total_gb, 2);
  printf(" | VRAM ");
  print_optional_float(telemetry->vram_used_gb, 2);
  printf("/");
  print_optional_float(telemetry->vram_total_gb, 2);
  printf("\n");
  printf("Fans RPM: CPU ");
  if (telemetry->fan_cpu == SYS_MON_FAN_RPM_UNAVAILABLE)
    printf("n/a");
  else
    printf("%u", telemetry->fan_cpu);
  printf(" | case ");
  if (telemetry->fan_case == SYS_MON_FAN_RPM_UNAVAILABLE)
    printf("n/a");
  else
    printf("%u", telemetry->fan_case);
  printf(" | GPU ");
  if (telemetry->fan_gpu == SYS_MON_FAN_RPM_UNAVAILABLE)
    printf("n/a\n");
  else
    printf("%u\n", telemetry->fan_gpu);
  printf("Power W: CPU ");
  print_optional_float(telemetry->cpu_power_w, 1);
  printf(" | GPU ");
  print_optional_float(telemetry->gpu_power_w, 1);
  printf("\nNetwork Mbps: down ");
  print_optional_float(telemetry->net_down_mbps, 2);
  printf(" | up ");
  print_optional_float(telemetry->net_up_mbps, 2);
  printf("\n");
  printf("Per-core CPU load (%u cores):", telemetry->core_count);
  for (uint8_t i = 0; i < telemetry->core_count; ++i) {
    printf(" %u=%.1f%%", i, telemetry->core_loads[i]);
  }
  printf("%s\n", telemetry->core_count ? "" : " not reported");
}

static void print_metric_value(const system_monitor_telemetry_t *t,
                               system_monitor_metric_t metric) {
  switch (metric) {
  case SYS_MON_METRIC_CPU_TEMP:
    printf("CPU temperature: ");
    print_optional_float(t->cpu_temp, 1);
    printf(" C\n");
    break;
  case SYS_MON_METRIC_GPU_TEMP:
    printf("GPU temperature: ");
    print_optional_float(t->gpu_temp, 1);
    printf(" C\n");
    break;
  case SYS_MON_METRIC_CPU_LOAD:
    printf("CPU load: ");
    print_optional_float(t->cpu_load, 1);
    printf("%%\n");
    break;
  case SYS_MON_METRIC_GPU_LOAD:
    printf("GPU load: ");
    print_optional_float(t->gpu_load, 1);
    printf("%%\n");
    break;
  case SYS_MON_METRIC_RAM:
    if (t->ram_total_gb > 0.0f && isfinite(t->ram_total_gb) &&
        isfinite(t->ram_used_gb)) {
      printf("RAM: %.2f / %.2f GB (%.1f%%)\n", t->ram_used_gb, t->ram_total_gb,
             t->ram_used_gb / t->ram_total_gb * 100.0f);
    } else {
      printf("RAM: ");
      print_optional_float(t->ram_used_gb, 2);
      printf(" / ");
      print_optional_float(t->ram_total_gb, 2);
      printf(" GB (usage unavailable)\n");
    }
    break;
  case SYS_MON_METRIC_VRAM:
    printf("VRAM: ");
    print_optional_float(t->vram_used_gb, 2);
    printf(" / ");
    print_optional_float(t->vram_total_gb, 2);
    printf(" GB\n");
    break;
  case SYS_MON_METRIC_SSD_TEMP:
    printf("SSD temperature: ");
    print_optional_float(t->ssd_temp, 1);
    printf(" C\n");
    break;
  case SYS_MON_METRIC_FAN_CPU:
    if (t->fan_cpu == SYS_MON_FAN_RPM_UNAVAILABLE)
      printf("CPU fan: n/a\n");
    else
      printf("CPU fan: %u RPM\n", t->fan_cpu);
    break;
  case SYS_MON_METRIC_NETWORK:
    printf("Network: down ");
    print_optional_float(t->net_down_mbps, 2);
    printf(" Mbps, up ");
    print_optional_float(t->net_up_mbps, 2);
    printf(" Mbps\n");
    break;
  case SYS_MON_METRIC_CORES:
    printf("Core loads (%u):", t->core_count);
    for (uint8_t i = 0; i < t->core_count; ++i) {
      printf(" %u:%.1f%%", i, t->core_loads[i]);
    }
    printf("\n");
    break;
  default:
    printf("Unknown metric.\n");
    break;
  }
}

static void print_status(void) {
  system_monitor_telemetry_t telemetry = {0};
  system_monitor_info_t info = {0};
  system_monitor_peaks_t peaks = {0};
  system_monitor_config_t config = {0};
  if (system_monitor_get_telemetry(&telemetry) != ESP_OK ||
      system_monitor_get_info(&info) != ESP_OK ||
      system_monitor_get_peaks(&peaks) != ESP_OK ||
      system_monitor_get_config(&config) != ESP_OK) {
    printf("ERROR: Could not read monitor state.\n");
    return;
  }

  printf("PC: %s | ", system_monitor_is_connected() ? "online" : "offline");
  print_telemetry(&telemetry);
  printf("Center metric: %s | auto-cycle: %s\n",
         metric_name(system_monitor_get_center_metric()),
         system_monitor_is_cycle_held() ? "held" : "running");
  printf("Hardware: CPU=%s | GPU=%s | RAM=%s | SSD=%s | NET=%s\n",
         info.cpu_name[0] ? info.cpu_name : "not reported",
         info.gpu_name[0] ? info.gpu_name : "not reported",
         info.ram_name[0] ? info.ram_name : "not reported",
         info.ssd_name[0] ? info.ssd_name : "not reported",
         info.net_name[0] ? info.net_name : "not reported");
  if (telemetry.timestamp_ms > 0) {
    print_metric_value(&telemetry, system_monitor_get_center_metric());
  } else {
    printf("Selected metric: no telemetry sample received\n");
  }
  uint32_t warnings = 0;
  const uint32_t alarms = system_monitor_get_alarms(&warnings);
  print_alarm_flags("Critical", alarms);
  print_alarm_flags("Warnings", warnings);
  printf("Peaks (60 samples): CPU/GPU/SSD ");
  print_optional_float(peaks.cpu_temp_peak, 1);
  printf("/");
  print_optional_float(peaks.gpu_temp_peak, 1);
  printf("/");
  print_optional_float(peaks.ssd_temp_peak, 1);
  printf(" C, CPU/GPU load ");
  print_optional_float(peaks.cpu_load_peak, 1);
  printf("/");
  print_optional_float(peaks.gpu_load_peak, 1);
  printf("%%\n");
  printf("Thresholds: CPU/GPU %.1f/%.1f C, RAM %.1f%%, fan stall < %u RPM; "
         "auto-cycle %lu ms\n",
         config.thresholds.cpu_temp_crit, config.thresholds.gpu_temp_crit,
         config.thresholds.ram_pct_crit, config.thresholds.fan_stall_min_rpm,
         (unsigned long)config.auto_cycle_interval_ms);
}

static void print_telemetry_command(void) {
  system_monitor_telemetry_t telemetry = {0};
  const esp_err_t err = system_monitor_get_telemetry(&telemetry);
  if (err != ESP_OK) {
    printf("ERROR: Could not read telemetry: %s\n", esp_err_to_name(err));
    return;
  }
  print_telemetry(&telemetry);
}

static void print_metrics(void) {
  printf("Selectable center metrics:\n");
  for (int i = 0; i < SYS_MON_METRIC_COUNT; ++i) {
    printf("  %d: %s\n", i, s_metric_names[i]);
  }
}

static void print_alarms(void) {
  uint32_t warnings = 0;
  uint32_t critical = system_monitor_get_alarms(&warnings);
  print_alarm_flags("Critical", critical);
  print_alarm_flags("Warnings", warnings);
  printf(
      "Critical flags: CPU temp, GPU temp, RAM, CPU fan stall, PC offline.\n");
  printf("Warnings start 10 C or 10 percentage points below the configured "
         "limit.\n");
  printf("Fanless systems can set fan-rpm=0 to disable the CPU fan-stall "
         "alarm.\n");
}

static void print_config(void) {
  system_monitor_config_t config = {0};
  if (system_monitor_get_config(&config) != ESP_OK) {
    printf("ERROR: Could not read monitor configuration.\n");
    return;
  }
  printf("cpu-temp=%.1f C, gpu-temp=%.1f C, ram-percent=%.1f%%, "
         "fan-rpm=%u, cycle-ms=%lu\n",
         config.thresholds.cpu_temp_crit, config.thresholds.gpu_temp_crit,
         config.thresholds.ram_pct_crit, config.thresholds.fan_stall_min_rpm,
         (unsigned long)config.auto_cycle_interval_ms);
  printf("Ranges: temperatures 0..200 C, RAM >0..100%%, fan 0..20000 RPM, "
         "cycle 0..3600000 ms (0 disables cycling).\n");
}

static bool parse_float_value(const char *text, float *value) {
  char *end = NULL;
  *value = strtof(text, &end);
  return end != text && *end == '\0';
}

static bool parse_uint_value(const char *text, uint32_t *value) {
  char *end = NULL;
  unsigned long parsed = strtoul(text, &end, 10);
  if (end == text || *end != '\0' || parsed > UINT32_MAX)
    return false;
  *value = (uint32_t)parsed;
  return true;
}

static void configure_monitor(const char *name, const char *value_text) {
  system_monitor_config_t config = {0};
  if (system_monitor_get_config(&config) != ESP_OK) {
    printf("ERROR: Could not read monitor configuration.\n");
    return;
  }

  if (strcmp(name, "cpu-temp") == 0 || strcmp(name, "gpu-temp") == 0 ||
      strcmp(name, "ram-percent") == 0) {
    float value;
    if (!parse_float_value(value_text, &value)) {
      printf("ERROR: Expected a numeric threshold.\n");
      return;
    }
    if (strcmp(name, "cpu-temp") == 0)
      config.thresholds.cpu_temp_crit = value;
    else if (strcmp(name, "gpu-temp") == 0)
      config.thresholds.gpu_temp_crit = value;
    else
      config.thresholds.ram_pct_crit = value;
  } else {
    uint32_t value;
    if (!parse_uint_value(value_text, &value)) {
      printf("ERROR: Expected an unsigned integer.\n");
      return;
    }
    if (strcmp(name, "fan-rpm") == 0 && value <= UINT16_MAX) {
      config.thresholds.fan_stall_min_rpm = (uint16_t)value;
    } else if (strcmp(name, "cycle-ms") == 0) {
      config.auto_cycle_interval_ms = value;
    } else {
      printf("ERROR: Unknown setting or value out of range.\n");
      return;
    }
  }

  const esp_err_t err = system_monitor_set_config(&config);
  if (err != ESP_OK) {
    printf("ERROR: Configuration rejected: %s\n", esp_err_to_name(err));
    return;
  }
  printf("Configuration updated.\n");
  print_config();
}

static void print_help(void) {
  printf("System Monitor Console commands:\n");
  printf("  help                         Show this help\n");
  printf("  status                       Show ALL telemetry, hardware, alarms, "
         "peaks, and config\n");
  printf("  telemetry                    Show every sensor value and per-core "
         "load\n");
  printf(
      "  metrics                      List every selectable center metric\n");
  printf("  alarms                       Show critical alarms and warnings\n");
  printf("  peaks                        Show 60-sample peak values\n");
  printf("  info                         Show reported hardware names\n");
  printf("  metric <name>                Select "
         "cpu-temp|gpu-temp|cpu-load|gpu-load|\n");
  printf("                               "
         "ram|vram|ssd-temp|fan-cpu|network|cores\n");
  printf("  config                       Show alarm thresholds and auto-cycle "
         "interval\n");
  printf("  config <name> <value>        Set cpu-temp, gpu-temp, ram-percent, "
         "fan-rpm,\n");
  printf(
      "                               or cycle-ms (0 disables auto-cycle)\n");
  printf("  config defaults              Restore this example's default "
         "configuration\n");
  printf("  cycle <hold|run|toggle>      Control automatic metric cycling "
         "directly\n");
  printf("  rotate <left|right> [pressed] Simulate encoder rotation\n");
  printf("  click [long]                 Simulate encoder short/long press\n");
  printf("  ack                          Acknowledge/snooze active alarms\n");
  printf(
      "  feed <packet>                Feed one !pc_hw or !pc_hw_info packet\n");
  printf("  demo <normal|warning|critical|partial> Inject portable sample "
         "telemetry\n");
  printf("  demo-info                    Inject sample hardware names\n");
  printf(
      "  tick <ms>                    Advance auto-cycle timer for testing\n");
  printf("  display brightness <0..100>  Set display backlight brightness\n");
  printf("  display theme <mono|cyan|amber> Set display theme\n");
  printf("  display lang <de|en>         Set display language\n");
  printf("  beep                         Simulate audio alarm chime\n");
  printf(
      "  !pc_hw ...                   Feed a raw telemetry packet directly\n");
  printf("  !pc_hw_info ...              Feed five pipe-delimited hardware "
         "names\n");
}

static void print_peaks(void) {
  system_monitor_peaks_t peaks = {0};
  const esp_err_t err = system_monitor_get_peaks(&peaks);
  if (err != ESP_OK) {
    printf("ERROR: Could not read peak values: %s\n", esp_err_to_name(err));
    return;
  }
  printf("Peak values: CPU ");
  print_optional_float(peaks.cpu_temp_peak, 1);
  printf(" C, GPU ");
  print_optional_float(peaks.gpu_temp_peak, 1);
  printf(" C, SSD ");
  print_optional_float(peaks.ssd_temp_peak, 1);
  printf(" C, CPU load ");
  print_optional_float(peaks.cpu_load_peak, 1);
  printf("%%, GPU load ");
  print_optional_float(peaks.gpu_load_peak, 1);
  printf("%%\n");
}

static void print_info(void) {
  system_monitor_info_t info = {0};
  const esp_err_t err = system_monitor_get_info(&info);
  if (err != ESP_OK) {
    printf("ERROR: Could not read hardware info: %s\n", esp_err_to_name(err));
    return;
  }
  printf("CPU: %s\nGPU: %s\nRAM: %s\nSSD: %s\nNetwork: %s\n", info.cpu_name,
         info.gpu_name, info.ram_name, info.ssd_name, info.net_name);
}

static void feed_packet(const char *packet) {
  const esp_err_t err = system_monitor_feed_serial_line(packet);
  if (err == ESP_OK)
    printf("Packet accepted.\n");
  else
    printf("ERROR: Packet rejected: %s\n", esp_err_to_name(err));
}

static void execute_command(char *line) {
  if (!line || !line[0])
    return;

  if (strncmp(line, "!pc_hw", 6) == 0) {
    feed_packet(line);
  } else if (strcmp(line, "help") == 0) {
    print_help();
  } else if (strcmp(line, "status") == 0) {
    print_status();
  } else if (strcmp(line, "telemetry") == 0) {
    print_telemetry_command();
  } else if (strcmp(line, "metrics") == 0) {
    print_metrics();
  } else if (strcmp(line, "alarms") == 0) {
    print_alarms();
  } else if (strcmp(line, "peaks") == 0) {
    print_peaks();
  } else if (strcmp(line, "info") == 0) {
    print_info();
  } else if (strcmp(line, "config") == 0) {
    print_config();
  } else if (strcmp(line, "config defaults") == 0) {
    system_monitor_config_t defaults = default_config();
    esp_err_t err = system_monitor_set_config(&defaults);
    if (err == ESP_OK) {
      printf("Default configuration restored.\n");
      print_config();
    } else {
      printf("ERROR: Could not restore defaults: %s\n", esp_err_to_name(err));
    }
  } else if (strncmp(line, "config ", 7) == 0) {
    char *name = line + 7;
    char *value = strchr(name, ' ');
    if (!value) {
      printf("Usage: config <cpu-temp|gpu-temp|ram-percent|fan-rpm|cycle-ms> "
             "<value>\n");
    } else {
      *value++ = '\0';
      while (*value == ' ')
        ++value;
      configure_monitor(name, value);
    }
  } else if (strncmp(line, "metric ", 7) == 0) {
    const char *name = line + 7;
    bool found = false;
    for (int i = 0; i < SYS_MON_METRIC_COUNT; ++i) {
      if (strcmp(name, s_metric_names[i]) == 0) {
        system_monitor_set_center_metric((system_monitor_metric_t)i);
        printf("Center metric set to %s.\n", name);
        found = true;
        break;
      }
    }
    if (!found)
      printf("ERROR: Unknown metric. Type 'help' to list names.\n");
  } else if (strncmp(line, "cycle ", 6) == 0) {
    const char *mode = line + 6;
    if (strcmp(mode, "hold") == 0) {
      system_monitor_set_cycle_held(true);
    } else if (strcmp(mode, "run") == 0) {
      system_monitor_set_cycle_held(false);
    } else if (strcmp(mode, "toggle") == 0) {
      system_monitor_set_cycle_held(!system_monitor_is_cycle_held());
    } else {
      printf("Usage: cycle <hold|run|toggle>\n");
      return;
    }
    printf("Automatic metric cycle is %s.\n",
           system_monitor_is_cycle_held() ? "held" : "running");
  } else if (strncmp(line, "rotate ", 7) == 0) {
    char *direction = line + 7;
    bool pressed = false;
    char *option = strchr(direction, ' ');
    if (option) {
      *option++ = '\0';
      pressed = strcmp(option, "pressed") == 0;
    }
    if (strcmp(direction, "left") == 0) {
      system_monitor_on_encoder_rotate(-1, pressed);
    } else if (strcmp(direction, "right") == 0) {
      system_monitor_on_encoder_rotate(1, pressed);
    } else {
      printf("Usage: rotate <left|right> [pressed]\n");
      return;
    }
    printf("Center metric: %s; cycle %s.\n",
           metric_name(system_monitor_get_center_metric()),
           system_monitor_is_cycle_held() ? "held" : "running");
  } else if (strcmp(line, "click") == 0) {
    system_monitor_on_encoder_click(false);
    printf("Short encoder click simulated.\n");
  } else if (strcmp(line, "click long") == 0) {
    system_monitor_on_encoder_click(true);
    printf(
        "Long encoder press simulated (standby hook logged by component).\n");
  } else if (strcmp(line, "ack") == 0) {
    system_monitor_acknowledge_alarm();
    printf("Alarms snoozed for %u seconds.\n", SYS_MON_ACK_SNOOZE_MS / 1000);
  } else if (strncmp(line, "feed ", 5) == 0) {
    feed_packet(line + 5);
  } else if (strncmp(line, "demo ", 5) == 0) {
    const char *kind = line + 5;
    if (strcmp(kind, "normal") == 0) {
      feed_packet("!pc_hw 1 54.0 48.0 42.0 25.0 15.0 8.0 32.0 4.0 8.0 1200 800 "
                  "1000 65.0 45.0 12.5 2.4 35.0,28.0,31.0,24.0");
    } else if (strcmp(kind, "warning") == 0) {
      feed_packet("!pc_hw 2 77.0 78.0 65.0 63.0 55.0 28.0 32.0 7.0 8.0 950 700 "
                  "900 110.0 95.0 70.0 20.0 80.0,76.0,72.0,65.0");
    } else if (strcmp(kind, "critical") == 0) {
      feed_packet("!pc_hw 3 92.0 90.0 78.0 85.0 82.0 31.0 32.0 7.5 8.0 0 600 "
                  "850 180.0 160.0 95.0 35.0 100.0,96.0,90.0,88.0");
    } else if (strcmp(kind, "partial") == 0) {
      feed_packet("!pc_hw 4 62.0 nan nan 30.0 nan 8.0 32.0 nan nan nan nan nan "
                  "75.0 nan nan nan 35.0,29.0,31.0,25.0");
    } else {
      printf("Usage: demo <normal|warning|critical|partial>\n");
    }
  } else if (strcmp(line, "demo-info") == 0) {
    feed_packet(
        "!pc_hw_info AMD Ryzen 9|NVIDIA RTX|64 GB DDR5|NVMe SSD|2.5 GbE");
  } else if (strncmp(line, "tick ", 5) == 0) {
    uint32_t elapsed_ms;
    if (!parse_uint_value(line + 5, &elapsed_ms)) {
      printf("Usage: tick <milliseconds>\n");
    } else {
      system_monitor_tick(elapsed_ms);
      printf("Advanced monitor tick by %lu ms.\n", (unsigned long)elapsed_ms);
    }
  } else if (strncmp(line, "display brightness ", 19) == 0) {
    uint32_t val = 0;
    if (parse_uint_value(line + 19, &val) && val <= 100) {
      system_monitor_display_set_brightness((uint8_t)val);
      printf("Display brightness set to %lu%%.\n", (unsigned long)val);
    } else {
      printf("Usage: display brightness <0..100>\n");
    }
  } else if (strncmp(line, "display theme ", 14) == 0) {
    const char *theme = line + 14;
    if (strcmp(theme, "mono") == 0) {
      system_monitor_display_set_theme(SYS_MON_THEME_MONO);
      printf("Display theme: Monochrome (High Contrast).\n");
    } else if (strcmp(theme, "cyan") == 0) {
      system_monitor_display_set_theme(SYS_MON_THEME_CYAN);
      printf("Display theme: Cyber Cyan.\n");
    } else if (strcmp(theme, "amber") == 0) {
      system_monitor_display_set_theme(SYS_MON_THEME_AMBER);
      printf("Display theme: Industrial Amber.\n");
    } else {
      printf("Usage: display theme <mono|cyan|amber>\n");
    }
  } else if (strncmp(line, "display lang ", 13) == 0) {
    const char *lang = line + 13;
    if (strcmp(lang, "de") == 0) {
      system_monitor_display_set_language(SYS_MON_LANG_DE);
      printf("Display Sprache: Deutsch.\n");
    } else if (strcmp(lang, "en") == 0) {
      system_monitor_display_set_language(SYS_MON_LANG_EN);
      printf("Display Language: English.\n");
    } else {
      printf("Usage: display lang <de|en>\n");
    }
  } else if (strcmp(line, "beep") == 0) {
    printf("\a[AUDIO ALARM CHIME]\n");
  } else {
    printf("Unknown command. Type 'help'.\n");
  }
}

static void display_task(void *pvParameters) {
  (void)pvParameters;
  ESP_LOGI(TAG, "Display task running (30 FPS GC9A01 refresh)");
  while (true) {
    system_monitor_display_render();
    vTaskDelay(pdMS_TO_TICKS(33)); // ~30 FPS
  }
}

static void console_line_handler(const char *line) {
  if (!line || !line[0])
    return;
  console_command_t command = {0};
  const size_t length = strlen(line);
  if (length >= sizeof(command.line)) {
    printf("\r\nERROR: Command exceeds %u bytes.\r\n", COMMAND_LINE_SIZE - 1);
    uart_utf8_console_request_refresh();
    return;
  }
  memcpy(command.line, line, length + 1);
  if (xQueueSend(s_command_queue, &command, 0) != pdTRUE) {
    printf("\r\nERROR: Command queue is full; try again.\r\n");
    uart_utf8_console_request_refresh();
  }
}

static void monitor_task(void *arg) {
  (void)arg;
  console_command_t command;
  int64_t previous_tick_ms = esp_timer_get_time() / 1000;
  while (true) {
    if (xQueueReceive(s_command_queue, &command, pdMS_TO_TICKS(50)) == pdTRUE) {
      execute_command(command.line);
      uart_utf8_console_notify_idle();
      // Drain any pending commands in queue immediately without delay
      while (xQueueReceive(s_command_queue, &command, 0) == pdTRUE) {
        execute_command(command.line);
        uart_utf8_console_notify_idle();
      }
    }
    int64_t now_ms = esp_timer_get_time() / 1000;
    uint32_t elapsed_ms = (uint32_t)(now_ms - previous_tick_ms);
    previous_tick_ms = now_ms;
    system_monitor_tick(elapsed_ms);
  }
}

void app_main(void) {
  system_monitor_config_t config = default_config();
  esp_err_t err = system_monitor_init(&config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "System Monitor init failed: %s", esp_err_to_name(err));
    return;
  }

  // Initialize GC9A01 240x240 circular display
  system_monitor_display_config_t disp_cfg =
      system_monitor_display_default_config();
  err = system_monitor_display_init(&disp_cfg);
  if (err != ESP_OK) {
    ESP_LOGW(TAG,
             "Display initialization failed: %s (continuing in headless "
             "console mode)",
             esp_err_to_name(err));
  } else {
    if (xTaskCreate(display_task, "sys_mon_disp", 6144, NULL, 4, NULL) !=
        pdPASS) {
      ESP_LOGE(TAG, "Could not create display task");
    }
  }

  s_command_queue =
      xQueueCreate(COMMAND_QUEUE_LENGTH, sizeof(console_command_t));
  if (!s_command_queue) {
    ESP_LOGE(TAG, "Could not create command queue");
    return;
  }

  err = uart_utf8_console_start("\x1b[36msystem-monitor>\x1b[0m ",
                                console_line_handler);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Could not start UART console: %s", esp_err_to_name(err));
    return;
  }

  if (xTaskCreate(monitor_task, "sys_mon_demo", 4096, NULL, 5, NULL) !=
      pdPASS) {
    ESP_LOGE(TAG, "Could not create monitor task");
    return;
  }
  ESP_LOGI(TAG, "Console ready. Type 'help'. Try 'demo normal', 'demo "
                "warning', or 'demo critical'.");
}
