async function toggleSerialConnection() {
  if (APP_STATE.connected) await disconnectSerial();
  else await connectSerial();
}

async function connectSerial() {
  if (!('serial' in navigator)) {
    showToast("Web Serial is not supported in this browser.");
    return;
  }
  const btn = document.getElementById('btnConnect');
  btn.classList.add('connecting');
  try {
    const port = await navigator.serial.requestPort();
    await port.open({ baudRate: 115200 });
    APP_STATE.port = port;
    APP_STATE.connected = true;
    resetDeviceReadings();
    setDeviceControlsEnabled(true);
    btn.classList.remove('connecting');
    btn.classList.add('connected');
    btn.dataset.tooltip = 'Disconnect ESP32';
    btn.setAttribute('aria-label', 'Disconnect ESP32');
    btn.setAttribute('aria-pressed', 'true');
    document.getElementById('connDot').classList.add('connected');
    document.getElementById('connLabel').textContent = "CONNECTED (115200)";
    setStatus("Serial connected", "connected");
    logTerminal("[SERIAL] Connected to ESP32 System Monitor @ 115200 Baud", "sys");
    startSerialReadLoop();
    sendSerialCmd("status");
  } catch (err) {
    btn.classList.remove('connecting');
    setStatus("Serial unavailable", "error");
    showToast(`Connection failed: ${err.message || 'Cancelled'}`);
  }
}

async function disconnectSerial() {
  if (APP_STATE.reader) {
    try {
      await APP_STATE.reader.cancel();
    } catch (error) {
      logTerminal(`[SERIAL ERROR] Could not stop reader: ${error.message}`, "err");
    }
    APP_STATE.reader = null;
  }
  if (APP_STATE.port) {
    try {
      await APP_STATE.port.close();
    } catch (error) {
      logTerminal(`[SERIAL ERROR] Could not close port: ${error.message}`, "err");
    }
  }
  APP_STATE.port = null;
  APP_STATE.connected = false;
  resetDeviceReadings();
  setDeviceControlsEnabled(false);
  const btn = document.getElementById('btnConnect');
  btn.classList.remove('connected', 'connecting');
  btn.dataset.tooltip = 'Connect ESP32 (115200 baud)';
  btn.setAttribute('aria-label', 'Connect ESP32');
  btn.setAttribute('aria-pressed', 'false');
  document.getElementById('connDot').classList.remove('connected');
  document.getElementById('connLabel').textContent = "WAITING FOR DEVICE";
  setStatus("Disconnected", "");
  logTerminal("[SERIAL] Disconnected.", "sys");
}

async function startSerialReadLoop() {
  const textDecoder = new TextDecoderStream();
  APP_STATE.port.readable.pipeTo(textDecoder.writable).catch((error) => {
    if (APP_STATE.connected) logTerminal(`[SERIAL ERROR] ${error.message}`, "err");
  });
  const reader = textDecoder.readable.getReader();
  APP_STATE.reader = reader;
  let lineBuffer = "";
  try {
    while (true) {
      const { value, done } = await reader.read();
      if (done) break;
      if (value) {
        lineBuffer += value;
        const lines = lineBuffer.split('\n');
        lineBuffer = lines.pop();
        for (const line of lines) {
          const clean = line.trim();
          if (clean) handleFirmwareLine(clean);
        }
      }
    }
  } catch (err) {
    logTerminal(`[SERIAL ERROR] ${err.message}`, "err");
  } finally {
    reader.releaseLock();
    if (APP_STATE.connected && APP_STATE.reader === reader) {
      APP_STATE.reader = null;
      await disconnectSerial();
    }
  }
}

function sendSerialCmd(cmd) {
  if (!APP_STATE.connected || !APP_STATE.port) {
    logTerminal(`Not sent (device disconnected): ${cmd}`, "err");
    showToast("Connect an ESP32 before sending commands.");
    return Promise.resolve(false);
  }
  logTerminal(cmd, "tx");
  return sendRawSerialCmd(cmd);
}

async function sendRawSerialCmd(cmd) {
  if (APP_STATE.connected && APP_STATE.port && APP_STATE.port.writable) {
    let writer;
    try {
      writer = APP_STATE.port.writable.getWriter();
      await writer.write(new TextEncoder().encode(cmd + "\r\n"));
      return true;
    } catch (e) {
      logTerminal(`[TX ERROR] ${e.message}`, "err");
      return false;
    } finally {
      if (writer) writer.releaseLock();
    }
  } else {
    logTerminal(`Not sent (device disconnected): ${cmd}`, "err");
    showToast("Connect an ESP32 before sending commands.");
    return false;
  }
}

function parseOptionalFloat(text) {
  if (!text || ['nan', 'n/a', 'unknown'].includes(text.toLowerCase().trim())) return null;
  const value = Number(text);
  return Number.isFinite(value) ? value : null;
}

function updateCoreAverage() {
  const values = APP_STATE.telemetry.core_loads.filter(Number.isFinite);
  APP_STATE.telemetry.core_load_avg = values.length
    ? values.reduce((total, value) => total + value, 0) / values.length
    : null;
}

function clearTelemetrySample() {
  Object.keys(APP_STATE.telemetry).forEach((key) => {
    if (key !== "sequence" && key !== "core_loads") APP_STATE.telemetry[key] = null;
  });
  APP_STATE.telemetry.core_loads = [];
  APP_STATE.peaks = {
    cpu_temp: null,
    gpu_temp: null,
    ssd_temp: null,
    cpu_load: null,
    gpu_load: null
  };
}

function resetDeviceReadings() {
  APP_STATE.lastPacketTime = 0;
  APP_STATE.activeMetric = null;
  APP_STATE.cycleHeld = null;
  APP_STATE.cycleIntervalMs = null;
  APP_STATE.pcOnline = null;
  APP_STATE.telemetry = {
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
  };
  APP_STATE.peaks = {
    cpu_temp: null,
    gpu_temp: null,
    ssd_temp: null,
    cpu_load: null,
    gpu_load: null
  };
  APP_STATE.info = {
    cpu_name: "",
    gpu_name: "",
    ram_name: "",
    ssd_name: "",
    net_name: ""
  };
  APP_STATE.thresholds = {
    cpu_temp_crit: null,
    gpu_temp_crit: null,
    ram_pct_crit: null,
    fan_stall_min_rpm: null
  };
  if (APP_STATE.alarmAckTimer) clearTimeout(APP_STATE.alarmAckTimer);
  APP_STATE.alarmAckTimer = null;
  APP_STATE.alarmAcknowledged = false;
  document.querySelectorAll('.metric-pill-btn').forEach((button) => {
    button.classList.remove('active');
  });
  updateTelemetrySummary();
  updateHardwareSummary();
  updateCycleControls();
}

function parseStatusTelemetryLine(line) {
  let match = line.match(/^Sequence:\s*(\d+)\s*\|\s*sample age:\s*(.+)$/i);
  if (match) {
    APP_STATE.telemetry.sequence = parseOptionalFloat(match[1]);
    const age = match[2].match(/^(\d+)\s*ms$/i);
    APP_STATE.lastPacketTime = age ? Date.now() - Number(age[1]) : 0;
    if (!age) {
      clearTelemetrySample();
    }
    updateTelemetrySummary();
    return;
  }

  match = line.match(/^Temperature C:\s*CPU\s+([^|]+)\|\s*GPU\s+([^|]+)\|\s*SSD\s+(.+)$/i);
  if (match) {
    APP_STATE.telemetry.cpu_temp = parseOptionalFloat(match[1].trim());
    APP_STATE.telemetry.gpu_temp = parseOptionalFloat(match[2].trim());
    APP_STATE.telemetry.ssd_temp = parseOptionalFloat(match[3].trim());
  }

  match = line.match(/^Load %:\s*CPU\s+([^|]+)\|\s*GPU\s+(.+)$/i);
  if (match) {
    APP_STATE.telemetry.cpu_load = parseOptionalFloat(match[1].trim());
    APP_STATE.telemetry.gpu_load = parseOptionalFloat(match[2].trim());
  }

  match = line.match(/^Memory GB:\s*RAM\s+([^|]+)\|\s*VRAM\s+(.+)$/i);
  if (match) {
    const ram = match[1].trim().split("/");
    const vram = match[2].trim().split("/");
    APP_STATE.telemetry.ram_used_gb = parseOptionalFloat(ram[0]);
    APP_STATE.telemetry.ram_total_gb = parseOptionalFloat(ram[1]);
    APP_STATE.telemetry.vram_used_gb = parseOptionalFloat(vram[0]);
    APP_STATE.telemetry.vram_total_gb = parseOptionalFloat(vram[1]);
  }

  match = line.match(/^Fans RPM:\s*CPU\s+([^|]+)\|\s*case\s+([^|]+)\|\s*GPU\s+(.+)$/i);
  if (match) {
    APP_STATE.telemetry.fan_cpu = parseFan(match[1].trim());
    APP_STATE.telemetry.fan_case = parseFan(match[2].trim());
    APP_STATE.telemetry.fan_gpu = parseFan(match[3].trim());
  }

  match = line.match(/^Power W:\s*CPU\s+([^|]+)\|\s*GPU\s+(.+)$/i);
  if (match) {
    APP_STATE.telemetry.cpu_power_w = parseOptionalFloat(match[1].trim());
    APP_STATE.telemetry.gpu_power_w = parseOptionalFloat(match[2].trim());
  }

  match = line.match(/^Network Mbps:\s*down\s+([^|]+)\|\s*up\s+(.+)$/i);
  if (match) {
    APP_STATE.telemetry.net_down_mbps = parseOptionalFloat(match[1].trim());
    APP_STATE.telemetry.net_up_mbps = parseOptionalFloat(match[2].trim());
  }

  if (line.startsWith("Per-core CPU load")) {
    APP_STATE.telemetry.core_loads = Array.from(
      line.matchAll(/\b\d+\s*[=:]\s*([-+]?(?:\d+(?:\.\d*)?|\.\d+))%/g),
      (core) => Number(core[1])
    );
    updateCoreAverage();
  }

  if (/^(Temperature C:|Load %:|Memory GB:|Fans RPM:|Power W:|Network Mbps:|Per-core CPU load)/i.test(line)) {
    updateTelemetrySummary();
    updatePeaks();
  }
}

function parseStatusHardwareLine(line) {
  const info = {};
  const fields = line.slice("Hardware:".length).split("|");
  const keys = { CPU: "cpu_name", GPU: "gpu_name", RAM: "ram_name", SSD: "ssd_name", NET: "net_name" };
  fields.forEach((field) => {
    const match = field.trim().match(/^(CPU|GPU|RAM|SSD|NET)=(.*)$/i);
    if (!match) return;
    const key = keys[match[1].toUpperCase()];
    const value = match[2].trim();
    info[key] = value.toLowerCase() === "not reported" ? "" : value;
  });
  APP_STATE.info = { ...APP_STATE.info, ...info };
  updateHardwareSummary();
}

function parseStatusConfigLine(line) {
  const match = line.match(/cpu-temp=([\d.]+) C,\s*gpu-temp=([\d.]+) C,\s*ram-percent=([\d.]+)%,\s*fan-rpm=(\d+),\s*cycle-ms=(\d+)/i);
  if (!match) return;
  APP_STATE.thresholds.cpu_temp_crit = Number(match[1]);
  APP_STATE.thresholds.gpu_temp_crit = Number(match[2]);
  APP_STATE.thresholds.ram_pct_crit = Number(match[3]);
  APP_STATE.thresholds.fan_stall_min_rpm = Number(match[4]);
  APP_STATE.cycleIntervalMs = Number(match[5]);
  updateCycleControls();
}

function handleFirmwareLine(line) {
  logTerminal(line, "rx");

  const pcStatusMatch = line.match(/^PC:\s*(online|offline)\s*\|/i);
  if (pcStatusMatch) {
    APP_STATE.pcOnline = pcStatusMatch[1].toLowerCase() === "online";
    updateTelemetrySummary();
  }

  if (line.startsWith("!pc_hw ")) {
    const parts = line.trim().split(/\s+/);
    if (parts.length === 18 || parts.length === 19) {
      APP_STATE.pcOnline = true;
      APP_STATE.telemetry.sequence = parseOptionalFloat(parts[1]);
      APP_STATE.telemetry.cpu_temp = parseOptionalFloat(parts[2]);
      APP_STATE.telemetry.gpu_temp = parseOptionalFloat(parts[3]);
      APP_STATE.telemetry.ssd_temp = parseOptionalFloat(parts[4]);
      APP_STATE.telemetry.cpu_load = parseOptionalFloat(parts[5]);
      APP_STATE.telemetry.gpu_load = parseOptionalFloat(parts[6]);
      APP_STATE.telemetry.ram_used_gb = parseOptionalFloat(parts[7]);
      APP_STATE.telemetry.ram_total_gb = parseOptionalFloat(parts[8]);
      APP_STATE.telemetry.vram_used_gb = parseOptionalFloat(parts[9]);
      APP_STATE.telemetry.vram_total_gb = parseOptionalFloat(parts[10]);
      APP_STATE.telemetry.fan_cpu = parseFan(parts[11]);
      APP_STATE.telemetry.fan_case = parseFan(parts[12]);
      APP_STATE.telemetry.fan_gpu = parseFan(parts[13]);
      APP_STATE.telemetry.cpu_power_w = parseOptionalFloat(parts[14]);
      APP_STATE.telemetry.gpu_power_w = parseOptionalFloat(parts[15]);
      APP_STATE.telemetry.net_down_mbps = parseOptionalFloat(parts[16]);
      APP_STATE.telemetry.net_up_mbps = parseOptionalFloat(parts[17]);
      APP_STATE.telemetry.core_loads = parts.length === 19
        ? parts[18].split(',').map(parseOptionalFloat).filter(v => v !== null)
        : [];
      updateCoreAverage();
      APP_STATE.lastPacketTime = Date.now();
      updatePeaks();
      updateTelemetrySummary();
      setStatus("Telemetry live", "connected");
    }
  }

  if (line.startsWith("!pc_hw_info ")) {
    const fields = line.slice("!pc_hw_info ".length).split('|').map(v => v.trim());
    if (fields.length === 5) {
      APP_STATE.info = { cpu_name: fields[0], gpu_name: fields[1], ram_name: fields[2], ssd_name: fields[3], net_name: fields[4] };
      updateHardwareSummary();
    }
  }

  parseStatusTelemetryLine(line);
  if (line.startsWith("Hardware:")) parseStatusHardwareLine(line);

  const thresholdMatch = line.match(/^Thresholds:\s*CPU\/GPU\s+([\d.]+)\/([\d.]+)\s*C,\s*RAM\s+([\d.]+)%,\s*fan stall <\s*(\d+)\s*RPM;\s*auto-cycle\s+(\d+)\s*ms/i);
  if (thresholdMatch) {
    APP_STATE.thresholds.cpu_temp_crit = Number(thresholdMatch[1]);
    APP_STATE.thresholds.gpu_temp_crit = Number(thresholdMatch[2]);
    APP_STATE.thresholds.ram_pct_crit = Number(thresholdMatch[3]);
    APP_STATE.thresholds.fan_stall_min_rpm = Number(thresholdMatch[4]);
    APP_STATE.cycleIntervalMs = Number(thresholdMatch[5]);
    updateCycleControls();
  } else {
    parseStatusConfigLine(line);
  }

  const metricMatch = line.match(/Center metric(?: set)?(?: to)?[: ]+([a-z0-9-]+)/i);
  if (metricMatch) {
    selectMetric(metricMatch[1]);
  }

  const cycleMatch = line.match(/auto-cycle:\s*(held|running)/i) ||
    line.match(/Automatic metric cycle is (held|running)/i);
  if (cycleMatch) {
    APP_STATE.cycleHeld = cycleMatch[1].toLowerCase() === "held";
    updateCycleControls();
  }

  const alarmAckMatch = line.match(/Alarms snoozed for (\d+) seconds/i);
  if (alarmAckMatch) {
    APP_STATE.alarmAcknowledged = true;
    if (APP_STATE.alarmAckTimer) clearTimeout(APP_STATE.alarmAckTimer);
    APP_STATE.alarmAckTimer = setTimeout(() => {
      APP_STATE.alarmAcknowledged = false;
    }, Number(alarmAckMatch[1]) * 1000);
  }
}

function parseFan(text) {
  if (!text) return null;
  const value = Number(text);
  return text.toLowerCase() === 'nan' || value === 65535 ? null : (Number.isFinite(value) ? value : null);
}

function updatePeaks() {
  for (const key of ['cpu_temp', 'gpu_temp', 'ssd_temp', 'cpu_load', 'gpu_load']) {
    const value = APP_STATE.telemetry[key];
    if (value !== null && Number.isFinite(value)) {
      if (APP_STATE.peaks[key] === null || value > APP_STATE.peaks[key]) APP_STATE.peaks[key] = value;
    }
  }
}

function formatValue(value, unit, digits = 1) {
  return value === null || value === undefined || !Number.isFinite(value)
    ? "Unknown"
    : `${value.toFixed(digits)}${unit}`;
}

function updateTelemetrySummary() {
  const t = APP_STATE.telemetry;
  const pcLinkStatus = document.getElementById('pcLinkStatus');
  pcLinkStatus.textContent = APP_STATE.pcOnline === null
    ? "Unknown"
    : APP_STATE.pcOnline
      ? "Online"
      : "Offline";
  pcLinkStatus.style.color = APP_STATE.pcOnline === null
    ? "var(--text-muted)"
    : APP_STATE.pcOnline
      ? "var(--accent-emerald-soft)"
      : "var(--accent-amber-soft)";
  document.getElementById('telemetrySequence').textContent = t.sequence === null ? "Unknown" : String(t.sequence);
  document.getElementById('telemetryTemps').textContent = `${formatValue(t.cpu_temp, " °C", 1)} / ${formatValue(t.gpu_temp, " °C", 1)}`;
  document.getElementById('telemetryLoads').textContent = `${formatValue(t.cpu_load, " %", 1)} / ${formatValue(t.gpu_load, " %", 1)}`;
  document.getElementById('telemetryMemory').textContent = `${formatValue(t.ram_used_gb, "", 1)} / ${formatValue(t.ram_total_gb, " GB", 1)}`;
  document.getElementById('telemetryFan').textContent = formatValue(t.fan_cpu, " RPM", 0);
  document.getElementById('telemetryNetwork').textContent = `${formatValue(t.net_down_mbps, " ↓ Mb/s", 1)} / ${formatValue(t.net_up_mbps, " ↑ Mb/s", 1)}`;
  const age = APP_STATE.lastPacketTime ? Date.now() - APP_STATE.lastPacketTime : null;
  const ageLabel = age === null
    ? "WAITING"
    : age >= APP_STATE.offlineTimeoutMs
      ? "STALE"
      : "LIVE";
  const ageColor = ageLabel === "LIVE"
    ? "var(--accent-emerald-soft)"
    : ageLabel === "STALE"
      ? "var(--accent-amber-soft)"
      : "var(--text-muted)";
  document.getElementById('telemetryAge').textContent = ageLabel;
  document.getElementById('telemetryAge').style.color = ageColor;
}

function updateHardwareSummary() {
  document.getElementById('infoCpu').textContent = APP_STATE.info.cpu_name || "Unknown";
  document.getElementById('infoGpu').textContent = APP_STATE.info.gpu_name || "Unknown";
  document.getElementById('infoRam').textContent = APP_STATE.info.ram_name || "Unknown";
  document.getElementById('infoSsd').textContent = APP_STATE.info.ssd_name || "Unknown";
  document.getElementById('infoNet').textContent = APP_STATE.info.net_name || "Unknown";
}

function setStatus(text, state) {
  document.getElementById('statusText').textContent = text;
  const dot = document.getElementById('statusDot');
  dot.classList.toggle('connected', state === 'connected');
  dot.classList.toggle('error', state === 'error');
  document.getElementById('statusInfo').textContent = APP_STATE.connected ? "ESP32 serial · 115200 baud" : "No serial device";
}

setInterval(() => {
  if (!APP_STATE.lastPacketTime) return;
  const age = Date.now() - APP_STATE.lastPacketTime;
  if (age >= APP_STATE.offlineTimeoutMs) {
    document.getElementById('telemetryAge').textContent = "STALE";
    document.getElementById('telemetryAge').style.color = "var(--accent-amber-soft)";
    setStatus("Telemetry stale", "error");
  } else {
    document.getElementById('telemetryAge').textContent = "LIVE";
    document.getElementById('telemetryAge').style.color = "var(--accent-emerald-soft)";
  }
}, 500);

function toggleTerminalDrawer(forceState) {
  const drawer = document.getElementById('bottomTerminalDrawer');
  const isOpen = forceState !== undefined ? forceState : !drawer.classList.contains('open');
  drawer.classList.toggle('open', isOpen);
  const toggleButton = document.getElementById('btnToggleTerm');
  toggleButton.classList.toggle('active', isOpen);
  toggleButton.setAttribute('aria-expanded', String(isOpen));
  if (isOpen) document.getElementById('termInput').focus();
}

function logTerminal(msg, tag = "sys") {
  const body = document.getElementById('termBody');
  if (!body) return;
  const row = document.createElement('div');
  row.className = 'terminal-row';
  const now = new Date();
  const ts = `${String(now.getHours()).padStart(2,'0')}:${String(now.getMinutes()).padStart(2,'0')}:${String(now.getSeconds()).padStart(2,'0')}.${String(Math.floor(now.getMilliseconds()/100))}`;
  row.innerHTML = `<span class="term-ts">[${ts}]</span><span class="term-tag ${tag}">[${tag.toUpperCase()}]</span><span class="term-msg">${escapeHtml(msg)}</span>`;
  body.appendChild(row);
  body.scrollTop = body.scrollHeight;
}

function clearTerminalLog() {
  document.getElementById('termBody').innerHTML = '';
}

function readTerminalLog() {
  const body = document.getElementById('termBody');
  return Array.from(body.children, (row) => {
    const fields = ['.term-ts', '.term-tag', '.term-msg'].map((selector) => {
      const field = row.querySelector(selector);
      return field ? field.textContent.trim() : '';
    });
    return fields.filter(Boolean).join(' ');
  }).join('\n');
}

async function copyTerminalLog() {
  try {
    await copyTextToClipboard(readTerminalLog());
  } catch (error) {
    logTerminal(`Could not copy serial log: ${error.message}`, "err");
  }
}

async function copyTextToClipboard(text) {
  if (navigator.clipboard && window.isSecureContext) {
    await navigator.clipboard.writeText(text);
    return;
  }

  const textarea = document.createElement('textarea');
  textarea.value = text;
  textarea.setAttribute('readonly', '');
  textarea.style.position = 'fixed';
  textarea.style.opacity = '0';
  document.body.appendChild(textarea);
  let copied;
  try {
    textarea.select();
    copied = document.execCommand('copy');
  } finally {
    textarea.remove();
  }
  if (!copied) throw new Error('Clipboard access is unavailable in this browser.');
}

function downloadTerminalLog() {
  let objectUrl;
  try {
    const blob = new Blob([readTerminalLog()], { type: 'text/plain;charset=utf-8' });
    objectUrl = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = objectUrl;
    link.download = 'system-monitor-serial-console-log.txt';
    document.body.appendChild(link);
    link.click();
    link.remove();
  } catch (error) {
    logTerminal(`Could not download serial log: ${error.message}`, "err");
  } finally {
    if (objectUrl) window.setTimeout(() => URL.revokeObjectURL(objectUrl), 1000);
  }
}

function setupTerminalInput() {
  const input = document.getElementById('termInput');
  input.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') sendCustomTerminalCmd();
  });
}

function sendCustomTerminalCmd() {
  const input = document.getElementById('termInput');
  const cmd = input.value.trim();
  if (!cmd) return;
  APP_STATE.cmdHistory.push(cmd);
  APP_STATE.historyIndex = -1;
  input.value = "";
  sendSerialCmd(cmd);
}

function openHelpModal() {
  document.getElementById('modalHelp').classList.add('active');
}

function closeHelpModal() {
  document.getElementById('modalHelp').classList.remove('active');
}

function showToast(msg) {
  const toast = document.getElementById('toastMsg');
  toast.textContent = msg;
  toast.classList.add('show');
  setTimeout(() => toast.classList.remove('show'), 2600);
}

function escapeHtml(str) {
  if (!str) return '';
  return String(str).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}
