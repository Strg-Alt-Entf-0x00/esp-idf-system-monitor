const APP_STATE = {
  connected: false,
  port: null,
  reader: null,
  theme: 1,
  language: 'en',
  brightness: 100,
  activeMetric: null,
  cycleHeld: null,
  cycleIntervalMs: null,
  pcOnline: null,
  lastPacketTime: 0,
  offlineTimeoutMs: 5000,
  telemetry: {
    sequence: null,
    cpu_temp: null,
    gpu_temp: null,
    ssd_temp: null,
    cpu_load: null,
    gpu_load: null,
    ram_used_gb: null,
    ram_total_gb: null,
    vram_used_gb: null,
    vram_total_gb: null,
    fan_cpu: null,
    fan_case: null,
    fan_gpu: null,
    cpu_power_w: null,
    gpu_power_w: null,
    net_down_mbps: null,
    net_up_mbps: null,
    core_loads: [],
    core_load_avg: null
  },
  peaks: {
    cpu_temp: null,
    gpu_temp: null,
    ssd_temp: null,
    cpu_load: null,
    gpu_load: null
  },
  info: {
    cpu_name: "",
    gpu_name: "",
    ram_name: "",
    ssd_name: "",
    net_name: ""
  },
  thresholds: {
    cpu_temp_crit: null,
    gpu_temp_crit: null,
    ram_pct_crit: null,
    fan_stall_min_rpm: null
  },
  alarmAckTimer: null,
  alarmAcknowledged: false,
  cmdHistory: [],
  historyIndex: -1
};

const METRIC_DEFS = [
  { id: 0, key: "cpu_temp", command: "cpu-temp", title_en: "CPU TEMPERATURE", title_de: "CPU TEMPERATUR", unit: "°C", min: 20, max: 100 },
  { id: 1, key: "gpu_temp", command: "gpu-temp", title_en: "GPU TEMPERATURE", title_de: "GPU TEMPERATUR", unit: "°C", min: 20, max: 100 },
  { id: 2, key: "cpu_load", command: "cpu-load", title_en: "CPU LOAD", title_de: "CPU AUSLASTUNG", unit: "%", min: 0, max: 100 },
  { id: 3, key: "gpu_load", command: "gpu-load", title_en: "GPU LOAD", title_de: "GPU AUSLASTUNG", unit: "%", min: 0, max: 100 },
  { id: 4, key: "ram_used_gb", command: "ram", title_en: "RAM USAGE", title_de: "RAM BELEGUNG", unit: "GB", min: 0, max: 32 },
  { id: 5, key: "vram_used_gb", command: "vram", title_en: "VRAM USAGE", title_de: "VRAM BELEGUNG", unit: "GB", min: 0, max: 24 },
  { id: 6, key: "ssd_temp", command: "ssd-temp", title_en: "SSD TEMPERATURE", title_de: "SSD TEMPERATUR", unit: "°C", min: 20, max: 90 },
  { id: 7, key: "fan_cpu", command: "fan-cpu", title_en: "CPU FAN", title_de: "CPU LUEFTER", unit: "RPM", min: 0, max: 2500 },
  { id: 8, key: "net_down_mbps", command: "network", title_en: "NETWORK", title_de: "NETZWERK", unit: "Mb/s", min: 0, max: 1000 },
  { id: 9, key: "core_load_avg", command: "cores", title_en: "CORE LOAD AVG", title_de: "KERNLAST AVG", unit: "%", min: 0, max: 100 }
];

const THEMES = [
  { bg: "#000000", track: "#212429", normal: "#ffffff", warn: "#f59e0b", crit: "#ef4444", text_primary: "#ffffff", text_secondary: "#94a3b8", peak: "#ffffff", center_fill: "#080c14" },
  { bg: "#000000", track: "#101e30", normal: "#38bdf8", warn: "#f59e0b", crit: "#ef4444", text_primary: "#ffffff", text_secondary: "#7dd3fc", peak: "#ffffff", center_fill: "#080c14" },
  { bg: "#000000", track: "#2a1e05", normal: "#fbbf24", warn: "#f59e0b", crit: "#ef4444", text_primary: "#ffffff", text_secondary: "#fde68a", peak: "#ffffff", center_fill: "#180f00" }
];

let canvas;
let ctx;

document.addEventListener('DOMContentLoaded', () => {
  canvas = document.getElementById('dispCanvas');
  ctx = canvas.getContext('2d');
  if (typeof setupTerminalInput === 'function') setupTerminalInput();
  if (typeof logTerminal === 'function') {
    logTerminal("System Monitor Studio ready. Waiting for firmware telemetry.", "sys");
  }
  requestAnimationFrame(renderDisplayLoop);
});
