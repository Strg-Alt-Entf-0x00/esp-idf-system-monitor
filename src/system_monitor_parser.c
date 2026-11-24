#include "system_monitor_parser.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "esp_timer.h"

#define SYS_MON_PACKET_MAX_LEN 512

static bool is_space(char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static char *skip_space(char *text)
{
    while (*text && is_space(*text)) ++text;
    return text;
}

static bool parse_float(const char *text, float *value, bool allow_nan)
{
    char *end = NULL;
    errno = 0;
    float parsed = strtof(text, &end);
    if (end == text || *end != '\0' || errno == ERANGE ||
        (!isfinite(parsed) && !(allow_nan && isnan(parsed)))) {
        return false;
    }
    *value = parsed;
    return true;
}

static bool is_nan_or_in_range(float value, float minimum, float maximum)
{
    return isnan(value) || (value >= minimum && value <= maximum);
}

static bool is_valid_memory_pair(float used, float total)
{
    if (isnan(used) || isnan(total)) {
        return isnan(used) && isnan(total);
    }
    return used <= total;
}

static bool parse_unsigned(const char *text, unsigned long maximum,
                           unsigned long *value)
{
    if (!text || !text[0] || text[0] == '-') return false;
    char *end = NULL;
    errno = 0;
    unsigned long parsed = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE || parsed > maximum) {
        return false;
    }
    *value = parsed;
    return true;
}

static bool parse_fan_rpm(const char *text, uint16_t *value)
{
    if (strcmp(text, "nan") == 0) {
        *value = SYS_MON_FAN_RPM_UNAVAILABLE;
        return true;
    }

    unsigned long parsed;
    if (!parse_unsigned(text, UINT16_MAX - 1UL, &parsed)) {
        return false;
    }
    *value = (uint16_t)parsed;
    return true;
}

static bool parse_core_loads(char *text, system_monitor_telemetry_t *out_data)
{
    if (!text || !text[0]) return false;
    char *cursor = text;
    while (cursor && *cursor) {
        if (out_data->core_count >= SYS_MON_MAX_CORES) return false;
        char *separator = strchr(cursor, ',');
        if (separator) *separator = '\0';
        float load;
        if (!parse_float(cursor, &load, false) || load < 0.0f || load > 100.0f) {
            return false;
        }
        out_data->core_loads[out_data->core_count++] = load;
        cursor = separator ? separator + 1 : NULL;
        if (cursor && !cursor[0]) return false;
    }
    return out_data->core_count > 0;
}

esp_err_t system_monitor_parse_hw_line(const char *line,
                                       system_monitor_telemetry_t *out_data)
{
    if (!line || !out_data) return ESP_ERR_INVALID_ARG;
    size_t line_length = strlen(line);
    if (line_length >= SYS_MON_PACKET_MAX_LEN) return ESP_ERR_INVALID_SIZE;

    char copy[SYS_MON_PACKET_MAX_LEN];
    memcpy(copy, line, line_length + 1);
    char *cursor = skip_space(copy);
    char *saveptr = NULL;
    char *tokens[19] = {0};
    size_t token_count = 0;
    for (char *token = strtok_r(cursor, " \t\r\n", &saveptr);
         token;
         token = strtok_r(NULL, " \t\r\n", &saveptr)) {
        if (token_count >= sizeof(tokens) / sizeof(tokens[0])) {
            return ESP_ERR_INVALID_ARG;
        }
        tokens[token_count++] = token;
    }

    if ((token_count != 18 && token_count != 19) ||
        strcmp(tokens[0], "!pc_hw") != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    system_monitor_telemetry_t parsed = {0};
    unsigned long sequence;
    if (!parse_unsigned(tokens[1], UINT32_MAX, &sequence)) {
        return ESP_ERR_INVALID_ARG;
    }
    parsed.sequence = (uint32_t)sequence;

    float *measurements[] = {
        &parsed.cpu_temp, &parsed.gpu_temp, &parsed.ssd_temp,
        &parsed.cpu_load, &parsed.gpu_load, &parsed.ram_used_gb,
        &parsed.ram_total_gb, &parsed.vram_used_gb, &parsed.vram_total_gb
    };
    for (size_t i = 0; i < sizeof(measurements) / sizeof(measurements[0]); ++i) {
        if (!parse_float(tokens[i + 2], measurements[i], true)) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (!is_nan_or_in_range(parsed.cpu_temp, -100.0f, 250.0f) ||
        !is_nan_or_in_range(parsed.gpu_temp, -100.0f, 250.0f) ||
        !is_nan_or_in_range(parsed.ssd_temp, -100.0f, 250.0f) ||
        !is_nan_or_in_range(parsed.cpu_load, 0.0f, 100.0f) ||
        !is_nan_or_in_range(parsed.gpu_load, 0.0f, 100.0f) ||
        !is_nan_or_in_range(parsed.ram_used_gb, 0.0f, INFINITY) ||
        !is_nan_or_in_range(parsed.ram_total_gb, 0.0f, INFINITY) ||
        !is_nan_or_in_range(parsed.vram_used_gb, 0.0f, INFINITY) ||
        !is_nan_or_in_range(parsed.vram_total_gb, 0.0f, INFINITY) ||
        !is_valid_memory_pair(parsed.ram_used_gb, parsed.ram_total_gb) ||
        !is_valid_memory_pair(parsed.vram_used_gb, parsed.vram_total_gb)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t fan_values[3];
    for (size_t i = 0; i < 3; ++i) {
        if (!parse_fan_rpm(tokens[i + 11], &fan_values[i])) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    parsed.fan_cpu = fan_values[0];
    parsed.fan_case = fan_values[1];
    parsed.fan_gpu = fan_values[2];

    float *power_and_network[] = {
        &parsed.cpu_power_w, &parsed.gpu_power_w,
        &parsed.net_down_mbps, &parsed.net_up_mbps
    };
    for (size_t i = 0; i < sizeof(power_and_network) / sizeof(power_and_network[0]); ++i) {
        if (!parse_float(tokens[i + 14], power_and_network[i], true)) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    for (size_t i = 0; i < sizeof(power_and_network) / sizeof(power_and_network[0]); ++i) {
        if (!is_nan_or_in_range(*power_and_network[i], 0.0f, INFINITY)) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    if (token_count == 19 && !parse_core_loads(tokens[18], &parsed)) {
        return ESP_ERR_INVALID_ARG;
    }

    parsed.timestamp_ms = esp_timer_get_time() / 1000;
    *out_data = parsed;
    return ESP_OK;
}

static void trim_field(char *field)
{
    size_t length = strlen(field);
    while (length > 0 && is_space(field[length - 1])) {
        field[--length] = '\0';
    }
    size_t first = 0;
    while (field[first] && is_space(field[first])) ++first;
    if (first > 0) memmove(field, field + first, strlen(field + first) + 1);
}

esp_err_t system_monitor_parse_info_line(const char *line,
                                         system_monitor_info_t *out_info)
{
    if (!line || !out_info) return ESP_ERR_INVALID_ARG;
    size_t line_length = strlen(line);
    if (line_length >= SYS_MON_PACKET_MAX_LEN) return ESP_ERR_INVALID_SIZE;

    char copy[SYS_MON_PACKET_MAX_LEN];
    memcpy(copy, line, line_length + 1);
    char *cursor = skip_space(copy);
    static const char prefix[] = "!pc_hw_info";
    if (strncmp(cursor, prefix, sizeof(prefix) - 1) != 0 ||
        (cursor[sizeof(prefix) - 1] &&
         !is_space(cursor[sizeof(prefix) - 1]))) {
        return ESP_ERR_INVALID_ARG;
    }
    cursor = skip_space(cursor + sizeof(prefix) - 1);
    size_t remaining = strlen(cursor);
    while (remaining > 0 && is_space(cursor[remaining - 1])) {
        cursor[--remaining] = '\0';
    }

    system_monitor_info_t parsed = {0};
    char *fields[] = {
        parsed.cpu_name, parsed.gpu_name, parsed.ram_name,
        parsed.ssd_name, parsed.net_name
    };
    const size_t field_sizes[] = {
        sizeof(parsed.cpu_name), sizeof(parsed.gpu_name), sizeof(parsed.ram_name),
        sizeof(parsed.ssd_name), sizeof(parsed.net_name)
    };
    char *field_start = cursor;
    for (size_t i = 0; i < 5; ++i) {
        char *separator = i < 4 ? strchr(field_start, '|') : NULL;
        if (i < 4 && !separator) return ESP_ERR_INVALID_ARG;
        if (i == 4 && strchr(field_start, '|')) return ESP_ERR_INVALID_ARG;
        size_t field_length = separator ? (size_t)(separator - field_start)
                                        : strlen(field_start);
        if (field_length >= field_sizes[i]) return ESP_ERR_INVALID_SIZE;
        memcpy(fields[i], field_start, field_length);
        fields[i][field_length] = '\0';
        trim_field(fields[i]);
        field_start = separator ? separator + 1 : field_start + field_length;
    }

    *out_info = parsed;
    return ESP_OK;
}
