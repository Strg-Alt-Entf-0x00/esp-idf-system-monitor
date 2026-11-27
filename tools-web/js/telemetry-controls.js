function metricIndexFromValue(id) {
  if (typeof id === 'string') {
    const value = id.toLowerCase();
    return METRIC_DEFS.findIndex(m =>
      m.command === value || m.key === value || m.title_en.toLowerCase().includes(value));
  }
  return Number.isInteger(id) && METRIC_DEFS[id] ? id : -1;
}

function selectMetric(id) {
  const index = metricIndexFromValue(id);
  if (index < 0) return;
  APP_STATE.activeMetric = index;
  document.querySelectorAll('.metric-pill-btn').forEach((btn, idx) => {
    btn.classList.toggle('active', idx === index);
  });
}

function requestMetric(id) {
  const metric = METRIC_DEFS[metricIndexFromValue(id)];
  if (!APP_STATE.connected || !metric) {
    showToast("Connect to the ESP32 before changing its metric.");
    return;
  }
  sendSerialCmd(`metric ${metric.command}`);
}

function setDeviceControlsEnabled(enabled) {
  document.querySelectorAll('.metric-pill-btn').forEach((button) => {
    button.disabled = !enabled;
  });
  ['btnPrevMetric', 'btnToggleCycle', 'btnNextMetric', 'btnEncoderClick', 'btnAlarmAck'].forEach((id) => {
    const button = document.getElementById(id);
    if (button) button.disabled = !enabled;
  });
  updateCycleControls();
}

function updateCycleControls() {
  const badge = document.getElementById('lblCycleStateBadge');
  const icon = document.getElementById('iconCycle');
  const slider = document.getElementById('rngCycleInterval');
  const value = document.getElementById('valCycleMs');
  const toggle = document.getElementById('btnToggleCycle');

  if (APP_STATE.cycleHeld === null) {
    badge.textContent = "UNKNOWN";
    badge.style.color = "var(--text-muted)";
  } else if (APP_STATE.cycleHeld) {
    badge.textContent = "HELD";
    badge.style.color = "var(--accent-amber-soft)";
  } else if (APP_STATE.cycleIntervalMs === 0) {
    badge.textContent = "DISABLED";
    badge.style.color = "var(--text-muted)";
  } else if (APP_STATE.cycleIntervalMs === null) {
    badge.textContent = "RUNNING · INTERVAL UNKNOWN";
    badge.style.color = "var(--accent-emerald-soft)";
  } else {
    badge.textContent = `RUNNING (${(APP_STATE.cycleIntervalMs / 1000).toFixed(1)}s)`;
    badge.style.color = "var(--accent-emerald-soft)";
  }

  if (APP_STATE.cycleIntervalMs !== null) {
    slider.value = String(APP_STATE.cycleIntervalMs);
    value.textContent = APP_STATE.cycleIntervalMs === 0
      ? "Disabled"
      : `${APP_STATE.cycleIntervalMs} ms`;
  } else {
    value.textContent = "Unknown";
  }
  slider.disabled = !APP_STATE.connected || APP_STATE.cycleIntervalMs === null;
  toggle.disabled = !APP_STATE.connected || APP_STATE.cycleHeld === null;
  if (icon) {
    icon.innerHTML = APP_STATE.cycleHeld || APP_STATE.cycleIntervalMs === 0
      ? `<polygon points="5 3 19 12 5 21 5 3"></polygon>`
      : `<rect x="6" y="4" width="4" height="16"></rect><rect x="14" y="4" width="4" height="16"></rect>`;
  }
}

function rotateEncoder(dir) {
  if (!APP_STATE.connected) {
    showToast("Connect to the ESP32 before using the encoder.");
    return;
  }
  sendSerialCmd(`rotate ${dir > 0 ? 'right' : 'left'}`);
}

function clickEncoder() {
  if (!APP_STATE.connected) {
    showToast("Connect to the ESP32 before using the encoder.");
    return;
  }
  sendSerialCmd("click");
}

function toggleCycle() {
  if (!APP_STATE.connected) {
    showToast("Connect to the ESP32 before changing auto-cycle.");
    return;
  }
  sendSerialCmd("cycle toggle");
}

function onCycleIntervalChange(val) {
  const ms = parseInt(val, 10);
  if (!Number.isInteger(ms) || ms < 0 || ms > 3600000 || !APP_STATE.connected) {
    updateCycleControls();
    showToast("Connect to the ESP32 to change its auto-cycle interval.");
    return;
  }
  document.getElementById('valCycleMs').textContent = "Pending...";
  sendSerialCmd(`config cycle-ms ${ms}`).then((sent) => {
    if (!sent) updateCycleControls();
  });
}

function ackAlarms() {
  if (!APP_STATE.connected) {
    showToast("Connect to the ESP32 before acknowledging alarms.");
    return;
  }
  sendSerialCmd("ack");
}

function setTheme(theme) {
  if (typeof theme === 'string') {
    theme = theme.toLowerCase();
    theme = theme === 'mono' ? 0 : (theme === 'amber' ? 2 : 1);
  }
  APP_STATE.theme = theme;
  document.getElementById('btnThemeMono').classList.toggle('active', theme === 0);
  document.getElementById('btnThemeCyan').classList.toggle('active', theme === 1);
  document.getElementById('btnThemeAmber').classList.toggle('active', theme === 2);
  sendSerialCmd(`display theme ${theme === 0 ? 'mono' : (theme === 1 ? 'cyan' : 'amber')}`);
}

function setLanguage(language) {
  APP_STATE.language = language;
  document.getElementById('btnLangEn').classList.toggle('active', language === 'en');
  document.getElementById('btnLangDe').classList.toggle('active', language === 'de');
  sendSerialCmd(`display lang ${language}`);
}

function setBrightness(value) {
  APP_STATE.brightness = parseInt(value, 10);
  document.getElementById('valBrightness').textContent = `${value}%`;
  canvas.style.filter = `brightness(${APP_STATE.brightness / 100.0})`;
  sendSerialCmd(`display brightness ${value}`);
}
