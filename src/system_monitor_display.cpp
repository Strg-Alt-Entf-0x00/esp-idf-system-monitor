#include "system_monitor_display.h"
#include "display_hal.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "system_monitor.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static const char *TAG = "sys_mon_disp";

namespace {

static system_monitor_display_config_t s_config;
static bool s_display_ready = false;

// Dynamic resolution geometry (reference 240x240)
static int s_width = 240;
static int s_height = 240;
static int s_cx = 120;
static int s_cy = 120;
static float s_scale = 1.0f;

// Color palettes
struct ThemeColors {
  uint16_t bg;
  uint16_t track;
  uint16_t normal;
  uint16_t warn;
  uint16_t crit;
  uint16_t text_primary;
  uint16_t text_secondary;
  uint16_t peak;
  uint16_t center_fill;
};

static const ThemeColors THEMES[] = {
    // SYS_MON_THEME_MONO (High-contrast pure black/white with warning/crit
    // accents)
    {0x0000, 0x2124, 0xFFFF, 0xFD00, 0xF800, 0xFFFF, 0x9CD3, 0xFFFF, 0x0821},
    // SYS_MON_THEME_CYAN (Cyber cyan accent)
    {0x0000, 0x10A2, 0x07FF, 0xFD00, 0xF800, 0xFFFF, 0x7BEF, 0xFFFF, 0x0821},
    // SYS_MON_THEME_AMBER (Industrial amber)
    {0x0000, 0x2100, 0xFDC0, 0xFD00, 0xF800, 0xFFFF, 0xBDD7, 0xFFFF, 0x1800},
};

// Metric smooth animation states
static float s_smooth_val[SYS_MON_METRIC_COUNT] = {};
static bool s_smooth_inited = false;

// Gauge angles
constexpr float GAUGE_START_DEG = 135.0f;
constexpr float GAUGE_SWEEP_DEG = 270.0f;

// Reusable scratch sprite for curved character rendering
static lgfx::LGFX_Sprite s_char_spr;
static bool s_char_spr_inited = false;

struct MetricInfo {
  const char *title_de;
  const char *title_en;
  const char *unit;
  float min_val;
  float max_val;
};

static const MetricInfo METRIC_DEFS[SYS_MON_METRIC_COUNT] = {
    {"CPU TEMPERATUR", "CPU TEMPERATURE", "°C", 20.0f, 100.0f},
    {"GPU TEMPERATUR", "GPU TEMPERATURE", "°C", 20.0f, 100.0f},
    {"CPU AUSLASTUNG", "CPU LOAD", "%", 0.0f, 100.0f},
    {"GPU AUSLASTUNG", "GPU LOAD", "%", 0.0f, 100.0f},
    {"RAM BELEGUNG", "RAM USAGE", "GB", 0.0f, 32.0f},
    {"VRAM BELEGUNG", "VRAM USAGE", "GB", 0.0f, 12.0f},
    {"SSD TEMPERATUR", "SSD TEMPERATURE", "°C", 20.0f, 90.0f},
    {"CPU LUEFTER", "CPU FAN", "RPM", 0.0f, 2500.0f},
    {"NETZWERK", "NETWORK", "Mb/s", 0.0f, 1000.0f},
    {"CPU KERNE", "CPU CORES", "%", 0.0f, 100.0f},
};

static inline float clamp_f(float v, float lo, float hi) {
  return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline float deg_to_rad(float deg) { return deg * (float)M_PI / 180.0f; }

static void draw_spoke(lgfx::LGFX_Sprite &spr, float r1, float r2, float deg,
                       uint16_t color) {
  const float rad = deg_to_rad(deg);
  const float sn = sinf(rad);
  const float cs = cosf(rad);
  spr.drawLine(s_cx + (int)roundf(cs * r1), s_cy + (int)roundf(sn * r1),
               s_cx + (int)roundf(cs * r2), s_cy + (int)roundf(sn * r2), color);
}

/**
 * @brief Render curved text along a circular arc with consistent Left-to-Right
 * reading
 *
 * @param target_spr Destination sprite
 * @param text UTF-8 / ASCII text string
 * @param center_deg Center angle in degrees (270 = top, 90 = bottom)
 * @param radius Arc radius in pixels from center
 * @param top_arc True if top arc (upright facing outwards), False if bottom arc
 * (upright facing center)
 * @param color Text color (RGB565)
 * @param font Pointer to LovyanGFX font
 * @param letter_spacing Extra pixel spacing between letters
 * @param font_zoom Proportional scaling factor for refined curved font size
 */
static void draw_curved_text(lgfx::LGFX_Sprite &target_spr, const char *text,
                             float center_deg, float radius, bool top_arc,
                             uint16_t color, const lgfx::IFont *font,
                             float letter_spacing = 1.2f,
                             float font_zoom = 0.72f) {
  if (!text || !text[0] || radius <= 5.0f)
    return;

  if (!s_char_spr_inited) {
    s_char_spr.setColorDepth(16);
    s_char_spr.createSprite(28, 28);
    s_char_spr.setTextDatum(lgfx::middle_center);
    s_char_spr.setPivot(14, 14);
    s_char_spr_inited = true;
  }

  if (font) {
    s_char_spr.setFont(font);
  } else {
    s_char_spr.setFont(&fonts::FreeSans9pt7b);
  }

  const size_t len = strlen(text);
  std::vector<float> char_angles(len);
  float total_angle = 0.0f;

  for (size_t i = 0; i < len; ++i) {
    char ch_buf[2] = {text[i], '\0'};
    float w = (text[i] == ' ') ? (float)s_char_spr.textWidth("a")
                               : (float)s_char_spr.textWidth(ch_buf);
    float a = (((w * font_zoom) + (letter_spacing * font_zoom)) / radius) *
              (180.0f / (float)M_PI);
    char_angles[i] = a;
    total_angle += a;
  }

  constexpr uint16_t TRANSP_KEY = 0x0001;

  if (top_arc) {
    // Top Arc (around 270°): text advances from left (<270°) to right (>270°)
    // In screen coordinates (Y down), angle <270° has cos < 0 (left), angle
    // >270° has cos > 0 (right). Increasing angle moves from Left to Right.
    float current_angle = center_deg - (total_angle / 2.0f);
    for (size_t i = 0; i < len; ++i) {
      const float a_step = char_angles[i];
      const float a_mid = current_angle + (a_step / 2.0f);
      current_angle += a_step;

      if (text[i] == ' ')
        continue;

      char ch_buf[2] = {text[i], '\0'};
      s_char_spr.fillSprite(TRANSP_KEY);
      s_char_spr.setTextColor(color, TRANSP_KEY);
      s_char_spr.drawString(ch_buf, 14, 14);

      const float rad = deg_to_rad(a_mid);
      const int px = s_cx + (int)roundf(cosf(rad) * radius);
      const int py = s_cy + (int)roundf(sinf(rad) * radius);

      // Upright facing outward: at 270° rot = 0; left tilts negative, right
      // tilts positive
      const float rot = a_mid - 270.0f;
      s_char_spr.pushRotateZoom(&target_spr, px, py, rot, font_zoom, font_zoom,
                                TRANSP_KEY);
    }
  } else {
    // Bottom Arc (around 90°): text advances from left (>90°) to right (<90°)
    // In screen coordinates (Y down), angle >90° has cos < 0 (left), angle <90°
    // has cos > 0 (right). To read Left to Right, angle must DECREASE!
    float current_angle = center_deg + (total_angle / 2.0f);
    for (size_t i = 0; i < len; ++i) {
      const float a_step = char_angles[i];
      const float a_mid = current_angle - (a_step / 2.0f);
      current_angle -= a_step;

      if (text[i] == ' ')
        continue;

      char ch_buf[2] = {text[i], '\0'};
      s_char_spr.fillSprite(TRANSP_KEY);
      s_char_spr.setTextColor(color, TRANSP_KEY);
      s_char_spr.drawString(ch_buf, 14, 14);

      const float rad = deg_to_rad(a_mid);
      const int px = s_cx + (int)roundf(cosf(rad) * radius);
      const int py = s_cy + (int)roundf(sinf(rad) * radius);

      // Upright facing inward (toward center): at 90° rot = 0; left (>90°)
      // tilts positive, right (<90°) tilts negative
      const float rot = a_mid - 90.0f;
      s_char_spr.pushRotateZoom(&target_spr, px, py, rot, font_zoom, font_zoom,
                                TRANSP_KEY);
    }
  }
}

/**
 * @brief Render center value and unit on a single horizontal baseline with
 * geometric degree symbol
 */
static void draw_center_value_and_unit(lgfx::LGFX_Sprite &spr,
                                       const char *val_str, const char *unit,
                                       uint16_t val_color, uint16_t unit_color,
                                       float scale) {
  if (!val_str)
    return;

  spr.setFont(&fonts::FreeSansBold18pt7b);
  const int val_w = spr.textWidth(val_str);

  const bool is_celsius =
      (unit && (strcmp(unit, "°C") == 0 || strcmp(unit, "C") == 0 ||
                strstr(unit, "C") != nullptr));
  const int gap = (int)roundf(4.0f * scale);
  const int y_base = s_cy + (int)roundf(6.0f * scale);

  if (is_celsius) {
    // Temperature layout: [VALUE] [°] [C] on one horizontal line
    // FreeSans9pt7b for "C", clean geometric circle for degree
    spr.setFont(&fonts::FreeSans9pt7b);
    const int c_w = spr.textWidth("C");
    const int deg_r = (scale >= 1.0f) ? 2 : 1;
    const int deg_span = deg_r * 2 + 3; // circle diameter + padding
    const int total_w = val_w + gap + deg_span + c_w;
    const int x_start = s_cx - (total_w / 2);

    // Draw Value
    spr.setFont(&fonts::FreeSansBold18pt7b);
    spr.setTextDatum(lgfx::baseline_left);
    spr.setTextColor(val_color);
    spr.drawString(val_str, x_start, y_base);

    // Draw geometric Degree circle at top-left of 'C'
    const int deg_cx = x_start + val_w + gap + deg_r + 1;
    const int deg_cy = y_base - (int)roundf(11.0f * scale);
    spr.drawCircle(deg_cx, deg_cy, deg_r, unit_color);
    if (deg_r > 1) {
      spr.drawCircle(deg_cx, deg_cy, deg_r - 1, unit_color);
    }

    // Draw 'C'
    spr.setFont(&fonts::FreeSans9pt7b);
    spr.setTextColor(unit_color);
    spr.drawString("C", x_start + val_w + gap + deg_span, y_base);
  } else {
    // Standard layout: [VALUE] [UNIT] on one horizontal line
    spr.setFont(&fonts::FreeSans9pt7b);
    const int unit_w = (unit && unit[0]) ? spr.textWidth(unit) : 0;
    const int total_w = (unit_w > 0) ? (val_w + gap + unit_w) : val_w;
    const int x_start = s_cx - (total_w / 2);

    // Draw Value
    spr.setFont(&fonts::FreeSansBold18pt7b);
    spr.setTextDatum(lgfx::baseline_left);
    spr.setTextColor(val_color);
    spr.drawString(val_str, x_start, y_base);

    // Draw Unit
    if (unit_w > 0) {
      spr.setFont(&fonts::FreeSans9pt7b);
      spr.setTextColor(unit_color);
      spr.drawString(unit, x_start + val_w + gap, y_base);
    }
  }
}

/**
 * @brief Draw disconnected PC monitor icon with a diagonal slash
 */
static void draw_pc_disconnected_icon(lgfx::LGFX_Sprite &spr, int cx, int cy,
                                      float scale, uint16_t color) {
  const int mw = (int)roundf(32.0f * scale);
  const int mh = (int)roundf(20.0f * scale);
  const int mx = cx - (mw / 2);
  const int my = cy - (mh / 2) - (int)roundf(2.0f * scale);

  // Screen frame
  spr.drawRoundRect(mx, my, mw, mh, 3, color);
  spr.drawRoundRect(mx + 1, my + 1, mw - 2, mh - 2, 2, color);

  // Stand neck
  spr.fillRect(cx - 1, my + mh, 3, (int)roundf(4.0f * scale), color);

  // Stand base
  const int bw = (int)roundf(16.0f * scale);
  spr.fillRect(cx - (bw / 2), my + mh + (int)roundf(4.0f * scale), bw, 2,
               color);

  // Diagonal slash (disconnected indicator)
  const int sx1 = mx - 2;
  const int sy1 = my - 2;
  const int sx2 = mx + mw + 2;
  const int sy2 = my + mh + 2;

  // Mask under-stroke with dark background to cleanly cut through monitor
  // border
  spr.drawLine(sx1 - 1, sy1, sx2 - 1, sy2, 0x0000);
  spr.drawLine(sx1 + 1, sy1, sx2 + 1, sy2, 0x0000);
  spr.drawLine(sx1, sy1 - 1, sx2, sy2 - 1, 0x0000);
  spr.drawLine(sx1, sy1 + 1, sx2, sy2 + 1, 0x0000);

  // Bright slash line
  spr.drawLine(sx1, sy1, sx2, sy2, color);
  spr.drawLine(sx1, sy1 + 1, sx2, sy2 + 1, color);
}

} // namespace

void system_monitor_display_set_geometry(int width, int height) {
  if (width > 0 && height > 0) {
    s_width = width;
    s_height = height;
    s_cx = width / 2;
    s_cy = height / 2;
    const int min_side = (width < height) ? width : height;
    s_scale = (float)min_side / 240.0f;
  }
}

float system_monitor_display_get_scale(void) { return s_scale; }

system_monitor_display_config_t system_monitor_display_default_config(void) {
  return (system_monitor_display_config_t){
      .pin_mosi = 2,
      .pin_sclk = 3,
      .pin_cs = 4,
      .pin_dc = 5,
      .pin_rst = 49,
      .pin_bkl = -1,
      .spi_host = SPI2_HOST,
      .spi_clock_hz = 40000000,
      .width = 240,
      .height = 240,
      .language = SYS_MON_LANG_EN, // Default to English as standard
      .theme = SYS_MON_THEME_MONO,
  };
}

esp_err_t
system_monitor_display_init(const system_monitor_display_config_t *config) {
  if (!config) {
    s_config = system_monitor_display_default_config();
  } else {
    s_config = *config;
  }

  system_monitor_display_set_geometry(s_config.width, s_config.height);

  esp_err_t ret = sys_mon_hal_init(&s_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "HAL init failed: %s", esp_err_to_name(ret));
    return ret;
  }

  s_display_ready = true;
  s_smooth_inited = false;
  ESP_LOGI(TAG, "System monitor display ready (%dx%d, scale=%.2f, lang=%s)",
           s_width, s_height, s_scale,
           s_config.language == SYS_MON_LANG_EN ? "EN" : "DE");
  return ESP_OK;
}

void system_monitor_display_set_brightness(uint8_t brightness) {
  sys_mon_hal_set_brightness(brightness);
}

void system_monitor_display_set_language(system_monitor_lang_t lang) {
  s_config.language = lang;
}

void system_monitor_display_set_theme(system_monitor_theme_t theme) {
  if (theme <= SYS_MON_THEME_AMBER) {
    s_config.theme = theme;
  }
}

esp_err_t system_monitor_display_deinit(void) {
  s_display_ready = false;
  if (s_char_spr_inited) {
    s_char_spr.deleteSprite();
    s_char_spr_inited = false;
  }
  return sys_mon_hal_deinit();
}

void system_monitor_display_render(void) {
  if (!s_display_ready)
    return;

  lgfx::LGFX_Sprite *spr_ptr = sys_mon_hal_get_sprite();
  if (!spr_ptr || !spr_ptr->getBuffer())
    return;
  lgfx::LGFX_Sprite &spr = *spr_ptr;

  // 1. Black Background (Pure 0x0000)
  spr.fillSprite(0x0000);

  const ThemeColors &colors = THEMES[s_config.theme];

  // 2. Fetch monitor data
  system_monitor_telemetry_t telem = {};
  system_monitor_info_t info = {};
  system_monitor_peaks_t peaks = {};
  system_monitor_config_t cfg = {};
  uint32_t warnings = 0;
  const uint32_t alarms = system_monitor_get_alarms(&warnings);
  const bool connected = system_monitor_is_connected();
  const bool cycle_held = system_monitor_is_cycle_held();
  const system_monitor_metric_t metric = system_monitor_get_center_metric();

  system_monitor_get_telemetry(&telem);
  system_monitor_get_info(&info);
  system_monitor_get_peaks(&peaks);
  system_monitor_get_config(&cfg);

  const int64_t now_us = esp_timer_get_time();
  const float now_sec = (float)now_us / 1000000.0f;

  // Outer perimeter bezel radius
  const int r_bezel = (int)(116.0f * s_scale);

  // 3. Alarm Bezel Pulse (Flashing outer perimeter on unacknowledged critical
  // alarm)
  const bool has_crit_alarm = (alarms & ~SYS_MON_ALARM_PC_OFFLINE) != 0;
  if (has_crit_alarm) {
    const float pulse = 0.5f + 0.5f * sinf(now_sec * 6.0f);
    if (pulse > 0.3f) {
      spr.drawCircle(s_cx, s_cy, r_bezel, colors.crit);
      spr.drawCircle(s_cx, s_cy, r_bezel - 1, colors.crit);
    }
  }

  // Dynamic radii for gauges (tighter to leave 18px clearance to glass bezel)
  const int r_gauge_outer = (int)(102.0f * s_scale);
  const int r_gauge_inner = (int)(90.0f * s_scale); // 12 px wide solid arc
  const int r_center_circle = (int)(38.0f * s_scale);

  // 4. Handle Offline Mode ("Kein PC")
  if (!connected) {
    // Red rotating beacon arc around center (filled arc)
    const float rot_deg = fmodf(now_sec * 120.0f, 360.0f);
    spr.fillArc(s_cx, s_cy, r_center_circle + 2, r_center_circle + 6, rot_deg,
                rot_deg + 90.0f, colors.crit);

    // Solid filled center circle
    spr.fillCircle(s_cx, s_cy, r_center_circle, colors.center_fill);
    spr.drawCircle(s_cx, s_cy, r_center_circle, colors.crit);

    // Blinking / pulsating disconnected PC icon
    const float pulse = 0.5f + 0.5f * sinf(now_sec * 5.0f);
    const uint16_t icon_color = (pulse > 0.3f) ? colors.crit : colors.warn;
    draw_pc_disconnected_icon(spr, s_cx, s_cy, s_scale, icon_color);

    // Curved text at Top Arc (270°) - comfortable clearance to outer gauge
    const char *top_str =
        (s_config.language == SYS_MON_LANG_DE) ? "KEIN PC" : "NO PC LINK";
    draw_curved_text(spr, top_str, 270.0f, 58.0f * s_scale, true, colors.crit,
                     &fonts::FreeSans9pt7b, 1.0f, 0.65f);

    // Curved text at Bottom Arc (90°) - reading Left-to-Right!
    const char *bot_str = (s_config.language == SYS_MON_LANG_DE)
                              ? "WARTE AUF PC"
                              : "WAITING FOR PC";
    draw_curved_text(spr, bot_str, 90.0f, 58.0f * s_scale, false,
                     colors.text_secondary, &fonts::FreeSans9pt7b, 1.0f, 0.65f);

    sys_mon_hal_push_frame();
    return;
  }

  // 5. Calculate metric values and alarm status for current metric
  float raw_val = 0.0f;
  float min_val = METRIC_DEFS[metric].min_val;
  float max_val = METRIC_DEFS[metric].max_val;
  float crit_thresh = NAN;
  float warn_thresh = NAN;
  float peak_val = NAN;
  bool metric_alarm = false;
  bool metric_warning = false;

  switch (metric) {
  case SYS_MON_METRIC_CPU_TEMP:
    raw_val = telem.cpu_temp;
    crit_thresh = cfg.thresholds.cpu_temp_crit;
    warn_thresh = crit_thresh - 10.0f;
    peak_val = peaks.cpu_temp_peak;
    metric_alarm = (alarms & SYS_MON_ALARM_CPU_TEMP) != 0;
    metric_warning = (warnings & SYS_MON_ALARM_CPU_TEMP) != 0;
    break;
  case SYS_MON_METRIC_GPU_TEMP:
    raw_val = telem.gpu_temp;
    crit_thresh = cfg.thresholds.gpu_temp_crit;
    warn_thresh = crit_thresh - 10.0f;
    peak_val = peaks.gpu_temp_peak;
    metric_alarm = (alarms & SYS_MON_ALARM_GPU_TEMP) != 0;
    metric_warning = (warnings & SYS_MON_ALARM_GPU_TEMP) != 0;
    break;
  case SYS_MON_METRIC_CPU_LOAD:
    raw_val = telem.cpu_load;
    peak_val = peaks.cpu_load_peak;
    break;
  case SYS_MON_METRIC_GPU_LOAD:
    raw_val = telem.gpu_load;
    peak_val = peaks.gpu_load_peak;
    break;
  case SYS_MON_METRIC_RAM:
    raw_val = telem.ram_used_gb;
    if (telem.ram_total_gb > 0.0f)
      max_val = telem.ram_total_gb;
    crit_thresh = max_val * (cfg.thresholds.ram_pct_crit / 100.0f);
    warn_thresh = max_val * ((cfg.thresholds.ram_pct_crit - 10.0f) / 100.0f);
    metric_alarm = (alarms & SYS_MON_ALARM_RAM) != 0;
    metric_warning = (warnings & SYS_MON_ALARM_RAM) != 0;
    break;
  case SYS_MON_METRIC_VRAM:
    raw_val = telem.vram_used_gb;
    if (telem.vram_total_gb > 0.0f)
      max_val = telem.vram_total_gb;
    break;
  case SYS_MON_METRIC_SSD_TEMP:
    raw_val = telem.ssd_temp;
    peak_val = peaks.ssd_temp_peak;
    crit_thresh = 75.0f;
    warn_thresh = 65.0f;
    break;
  case SYS_MON_METRIC_FAN_CPU:
    raw_val = (telem.fan_cpu == SYS_MON_FAN_RPM_UNAVAILABLE)
                  ? 0.0f
                  : (float)telem.fan_cpu;
    crit_thresh = (float)cfg.thresholds.fan_stall_min_rpm;
    metric_alarm = (alarms & SYS_MON_ALARM_FAN_CPU) != 0;
    break;
  case SYS_MON_METRIC_NETWORK:
    raw_val = telem.net_down_mbps;
    if (raw_val > 1000.0f)
      max_val = 2500.0f;
    else if (raw_val > 500.0f)
      max_val = 1000.0f;
    else
      max_val = 500.0f;
    break;
  case SYS_MON_METRIC_CORES:
    raw_val = telem.cpu_load; // Single ring representation for overall load
    peak_val = peaks.cpu_load_peak;
    break;
  default:
    break;
  }

  if (!s_smooth_inited) {
    for (int i = 0; i < SYS_MON_METRIC_COUNT; ++i)
      s_smooth_val[i] = raw_val;
    s_smooth_inited = true;
  }
  // Smooth value interpolation
  if (std::isfinite(raw_val)) {
    s_smooth_val[metric] += (raw_val - s_smooth_val[metric]) * 0.25f;
  }
  const float current_disp_val = s_smooth_val[metric];

  // Compute normalized fraction
  const float norm_frac = clamp_f((current_disp_val - min_val) /
                                      std::max(1.0f, (max_val - min_val)),
                                  0.0f, 1.0f);

  // Active color choice
  uint16_t active_color = colors.normal;
  if (metric_alarm)
    active_color = colors.crit;
  else if (metric_warning)
    active_color = colors.warn;

  // 6. SOLID Outer Gauge Arc (270° Sweep from 135° to 405°)
  // Fully filled solid background track (12 px wide, tighter radius leaves 18px
  // bezel margin)
  spr.fillArc(s_cx, s_cy, r_gauge_inner, r_gauge_outer, GAUGE_START_DEG,
              GAUGE_START_DEG + GAUGE_SWEEP_DEG, colors.track);

  // Fully filled solid active value arc (12 px wide)
  if (norm_frac > 0.005f) {
    const float sweep_angle = GAUGE_SWEEP_DEG * norm_frac;
    spr.fillArc(s_cx, s_cy, r_gauge_inner, r_gauge_outer, GAUGE_START_DEG,
                GAUGE_START_DEG + sweep_angle, active_color);
  }

  // Radial scale tick marks (20 ticks inside the track, major every 5)
  const int r_tick_outer = r_gauge_inner - 2;
  const int r_tick_maj = r_tick_outer - (int)(7.0f * s_scale);
  const int r_tick_min = r_tick_outer - (int)(4.0f * s_scale);
  for (int i = 0; i <= 20; ++i) {
    const bool major = (i % 5 == 0);
    const float tick_deg = GAUGE_START_DEG + 13.5f * i;
    const uint16_t t_col = major ? colors.text_secondary : colors.track;
    draw_spoke(spr, major ? r_tick_maj : r_tick_min, r_tick_outer, tick_deg,
               t_col);
  }

  // Draw peak spoke indicator (over 60 samples)
  if (std::isfinite(peak_val) && peak_val > current_disp_val) {
    const float peak_frac = clamp_f(
        (peak_val - min_val) / std::max(1.0f, (max_val - min_val)), 0.0f, 1.0f);
    const float peak_deg = GAUGE_START_DEG + GAUGE_SWEEP_DEG * peak_frac;
    draw_spoke(spr, r_gauge_inner - 2, r_gauge_outer + 2, peak_deg,
               colors.peak);
  }

  // Draw threshold ticks on outer gauge
  if (std::isfinite(warn_thresh)) {
    const float w_frac =
        clamp_f((warn_thresh - min_val) / std::max(1.0f, (max_val - min_val)),
                0.0f, 1.0f);
    draw_spoke(spr, r_gauge_inner - 3, r_gauge_outer + 3,
               GAUGE_START_DEG + GAUGE_SWEEP_DEG * w_frac, colors.warn);
  }
  if (std::isfinite(crit_thresh)) {
    const float c_frac =
        clamp_f((crit_thresh - min_val) / std::max(1.0f, (max_val - min_val)),
                0.0f, 1.0f);
    draw_spoke(spr, r_gauge_inner - 4, r_gauge_outer + 4,
               GAUGE_START_DEG + GAUGE_SWEEP_DEG * c_frac, colors.crit);
  }

  // 7. Center Value & Unit on a single horizontal line with geometric degree
  // circle
  char val_str[32] = {};
  if (!std::isfinite(current_disp_val)) {
    snprintf(val_str, sizeof(val_str), "--");
  } else if (metric == SYS_MON_METRIC_RAM || metric == SYS_MON_METRIC_VRAM ||
             metric == SYS_MON_METRIC_NETWORK) {
    snprintf(val_str, sizeof(val_str), "%.1f", (double)current_disp_val);
  } else {
    snprintf(val_str, sizeof(val_str), "%d", (int)roundf(current_disp_val));
  }

  draw_center_value_and_unit(spr, val_str, METRIC_DEFS[metric].unit,
                             active_color, colors.text_secondary, s_scale);

  // 8. Top Curved Header: Metric Title (e.g. "CPU TEMPERATURE") - refined
  // spacing from outer gauge
  const char *title = (s_config.language == SYS_MON_LANG_DE)
                          ? METRIC_DEFS[metric].title_de
                          : METRIC_DEFS[metric].title_en;
  const uint16_t top_color =
      metric_alarm ? colors.crit
                   : (metric_warning ? colors.warn : colors.text_primary);
  draw_curved_text(spr, title, 270.0f, 58.0f * s_scale, true, top_color,
                   &fonts::FreeSans9pt7b, 1.0f, 0.65f);

  // 9. Bottom Curved Subtitle: Hardware Name or Status - reading Left-to-Right!
  const char *subtitle = "";
  uint16_t sub_color = colors.text_secondary;
  if (metric_alarm) {
    subtitle = "! ALARM !";
    sub_color = colors.crit;
  } else if (metric_warning) {
    subtitle = (s_config.language == SYS_MON_LANG_DE) ? "WARNUNG" : "WARNING";
    sub_color = colors.warn;
  } else if (cycle_held) {
    subtitle = (s_config.language == SYS_MON_LANG_DE) ? "FESTGEHALTEN" : "HOLD";
    sub_color = colors.warn;
  } else {
    switch (metric) {
    case SYS_MON_METRIC_CPU_TEMP:
    case SYS_MON_METRIC_CPU_LOAD:
    case SYS_MON_METRIC_CORES:
      subtitle = info.cpu_name[0] ? info.cpu_name : "CPU";
      break;
    case SYS_MON_METRIC_GPU_TEMP:
    case SYS_MON_METRIC_GPU_LOAD:
      subtitle = info.gpu_name[0] ? info.gpu_name : "GPU";
      break;
    case SYS_MON_METRIC_RAM:
      subtitle = info.ram_name[0] ? info.ram_name : "RAM";
      break;
    case SYS_MON_METRIC_VRAM:
      subtitle = info.gpu_name[0] ? info.gpu_name : "VRAM";
      break;
    case SYS_MON_METRIC_SSD_TEMP:
      subtitle = info.ssd_name[0] ? info.ssd_name : "NVMe SSD";
      break;
    case SYS_MON_METRIC_FAN_CPU:
      subtitle = "CPU Cooler";
      break;
    case SYS_MON_METRIC_NETWORK:
      subtitle = info.net_name[0] ? info.net_name : "Ethernet";
      break;
    default:
      subtitle = "";
      break;
    }
  }
  const size_t sub_len = strlen(subtitle);
  float sub_zoom = 0.65f;
  if (sub_len > 12) {
    sub_zoom = (12.0f / (float)sub_len) * 0.65f;
    if (sub_zoom < 0.48f)
      sub_zoom = 0.48f;
  }
  draw_curved_text(spr, subtitle, 90.0f, 58.0f * s_scale, false, sub_color,
                   &fonts::FreeSans9pt7b, 1.0f, sub_zoom);

  // 10. Push frame to GC9A01 via DMA
  sys_mon_hal_push_frame();
}
