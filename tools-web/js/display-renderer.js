function renderDisplayLoop() {
      renderGC9A01Frame();
      requestAnimationFrame(renderDisplayLoop);
    }

    function renderGC9A01Frame() {
      const now = Date.now();
      const nowSec = now / 1000.0;
      const themeColors = THEMES[APP_STATE.theme] || THEMES[1];
      const isOnline = (now - APP_STATE.lastPacketTime < APP_STATE.offlineTimeoutMs);

      // Clear Canvas to pure black
      ctx.fillStyle = "#000000";
      ctx.fillRect(0, 0, 240, 240);

      const cx = 120, cy = 120;

      // 1. Alarm Bezel Pulse on Critical Alarm
      const cpuTemp = APP_STATE.telemetry.cpu_temp;
      const gpuTemp = APP_STATE.telemetry.gpu_temp;
      const cpuCrit = APP_STATE.thresholds.cpu_temp_crit;
      const gpuCrit = APP_STATE.thresholds.gpu_temp_crit;
      const isCritical = ((Number.isFinite(cpuCrit) && cpuTemp !== null && Number.isFinite(cpuTemp) && cpuTemp >= cpuCrit) ||
                          (Number.isFinite(gpuCrit) && gpuTemp !== null && Number.isFinite(gpuTemp) && gpuTemp >= gpuCrit));
      if (isCritical && !APP_STATE.alarmAcknowledged) {
        const pulse = 0.5 + 0.5 * Math.sin(nowSec * 6.0);
        if (pulse > 0.3) {
          ctx.beginPath();
          ctx.arc(cx, cy, 116, 0, Math.PI * 2);
          ctx.strokeStyle = themeColors.crit;
          ctx.lineWidth = 2;
          ctx.stroke();
        }
      }

      // 2. Offline Mode ("NO PC LINK" / "KEIN PC")
      if (!isOnline) {
        renderOfflineScreen(cx, cy, nowSec, themeColors);
        return;
      }

      // 3. Online Gauge Rendering
      const curDef = METRIC_DEFS[APP_STATE.activeMetric];
      if (!curDef) {
        renderAwaitingMetric(cx, cy, themeColors);
        return;
      }
      let val = APP_STATE.telemetry[curDef.key];
      if (curDef.key === 'fan_cpu' && val === 65535) val = null;
      const hasValue = val !== null && val !== undefined && Number.isFinite(val);
      if (!hasValue) val = null;
      let minVal = curDef.min, maxVal = curDef.max;
      if (curDef.key === 'ram_used_gb' && APP_STATE.telemetry.ram_total_gb !== null) {
        maxVal = APP_STATE.telemetry.ram_total_gb;
      }
      if (curDef.key === 'vram_used_gb' && APP_STATE.telemetry.vram_total_gb !== null) {
        maxVal = APP_STATE.telemetry.vram_total_gb;
      }
      if (curDef.key === 'net_down_mbps') {
        if (hasValue && val > 1000) maxVal = 2500;
        else if (hasValue && val > 500) maxVal = 1000;
        else maxVal = 500;
      }

      // Alarm / Warning status for active metric
      let isMetricAlarm = false, isMetricWarn = false;
      let warnThresh = null, critThresh = null;

      if (curDef.key === 'cpu_temp' && hasValue && Number.isFinite(APP_STATE.thresholds.cpu_temp_crit)) {
        critThresh = APP_STATE.thresholds.cpu_temp_crit;
        warnThresh = critThresh - 10.0;
        if (val >= critThresh) isMetricAlarm = true;
        else if (val >= warnThresh) isMetricWarn = true;
      } else if (curDef.key === 'gpu_temp' && hasValue && Number.isFinite(APP_STATE.thresholds.gpu_temp_crit)) {
        critThresh = APP_STATE.thresholds.gpu_temp_crit;
        warnThresh = critThresh - 10.0;
        if (val >= critThresh) isMetricAlarm = true;
        else if (val >= warnThresh) isMetricWarn = true;
      } else if (curDef.key === 'ram_used_gb' && hasValue && Number.isFinite(APP_STATE.thresholds.ram_pct_crit)) {
        critThresh = maxVal * (APP_STATE.thresholds.ram_pct_crit / 100.0);
        warnThresh = maxVal * ((APP_STATE.thresholds.ram_pct_crit - 10.0) / 100.0);
        if (val >= critThresh) isMetricAlarm = true;
        else if (val >= warnThresh) isMetricWarn = true;
      }

      const activeColor = isMetricAlarm ? themeColors.crit : (isMetricWarn ? themeColors.warn : themeColors.normal);

      // Gauge Geometry: r_outer=102, r_inner=90 (12px arc width), start=135°, sweep=270°
      const rOuter = 102, rInner = 90;
      const startDeg = 135, sweepDeg = 270;
      const startRad = (startDeg * Math.PI) / 180;
      const sweepRad = (sweepDeg * Math.PI) / 180;

      // Draw Solid Background Track
      drawSolidArc(cx, cy, rInner, rOuter, startRad, startRad + sweepRad, themeColors.track);

      // Draw Active Value Arc
      const normFrac = hasValue ? Math.max(0, Math.min(1, (val - minVal) / Math.max(1, maxVal - minVal))) : 0;
      if (normFrac > 0.005) {
        drawSolidArc(cx, cy, rInner, rOuter, startRad, startRad + (sweepRad * normFrac), activeColor);
      }

      // Draw 20 Radial Scale Ticks
      const rTickOuter = rInner - 2;
      for (let i = 0; i <= 20; i++) {
        const major = (i % 5 === 0);
        const tickDeg = startDeg + (13.5 * i);
        const tickRad = (tickDeg * Math.PI) / 180;
        const rTickInner = major ? (rTickOuter - 7) : (rTickOuter - 4);
        const tickCol = major ? themeColors.text_secondary : themeColors.track;

        ctx.beginPath();
        ctx.moveTo(cx + Math.cos(tickRad) * rTickInner, cy + Math.sin(tickRad) * rTickInner);
        ctx.lineTo(cx + Math.cos(tickRad) * rTickOuter, cy + Math.sin(tickRad) * rTickOuter);
        ctx.strokeStyle = tickCol;
        ctx.lineWidth = major ? 1.5 : 1;
        ctx.stroke();
      }

      // Draw Peak Marker Spoke
      let peakVal = APP_STATE.peaks[curDef.key];
      if (peakVal !== null && peakVal !== undefined && hasValue && peakVal > val) {
        const peakFrac = Math.max(0, Math.min(1, (peakVal - minVal) / Math.max(1, maxVal - minVal)));
        const peakRad = ((startDeg + sweepDeg * peakFrac) * Math.PI) / 180;
        ctx.beginPath();
        ctx.moveTo(cx + Math.cos(peakRad) * (rInner - 2), cy + Math.sin(peakRad) * (rInner - 2));
        ctx.lineTo(cx + Math.cos(peakRad) * (rOuter + 2), cy + Math.sin(peakRad) * (rOuter + 2));
        ctx.strokeStyle = themeColors.peak;
        ctx.lineWidth = 2;
        ctx.stroke();
      }

      // Draw Threshold Ticks (Warning & Critical)
      if (warnThresh !== null) {
        const wFrac = Math.max(0, Math.min(1, (warnThresh - minVal) / Math.max(1, maxVal - minVal)));
        const wRad = ((startDeg + sweepDeg * wFrac) * Math.PI) / 180;
        ctx.beginPath();
        ctx.moveTo(cx + Math.cos(wRad) * (rInner - 3), cy + Math.sin(wRad) * (rInner - 3));
        ctx.lineTo(cx + Math.cos(wRad) * (rOuter + 3), cy + Math.sin(wRad) * (rOuter + 3));
        ctx.strokeStyle = themeColors.warn;
        ctx.lineWidth = 1.8;
        ctx.stroke();
      }
      if (critThresh !== null) {
        const cFrac = Math.max(0, Math.min(1, (critThresh - minVal) / Math.max(1, maxVal - minVal)));
        const cRad = ((startDeg + sweepDeg * cFrac) * Math.PI) / 180;
        ctx.beginPath();
        ctx.moveTo(cx + Math.cos(cRad) * (rInner - 4), cy + Math.sin(cRad) * (rInner - 4));
        ctx.lineTo(cx + Math.cos(cRad) * (rOuter + 4), cy + Math.sin(cRad) * (rOuter + 4));
        ctx.strokeStyle = themeColors.crit;
        ctx.lineWidth = 2;
        ctx.stroke();
      }

      // Center Value & Unit on Single Horizontal Line
      drawCenterValueAndUnit(cx, cy, val, curDef.unit, activeColor, themeColors.text_secondary);

      // Top Curved Text: Metric Title (e.g. CPU TEMPERATURE)
      const topTitle = (APP_STATE.language === 'de') ? curDef.title_de : curDef.title_en;
      const topColor = isMetricAlarm ? themeColors.crit : (isMetricWarn ? themeColors.warn : themeColors.text_primary);
      drawCurvedText(cx, cy, topTitle, 58, 270, true, topColor, "bold 9px var(--font-sans)");

      // Bottom Curved Text: Hardware Subtitle or Status
      let botSubtitle = "";
      let botColor = themeColors.text_secondary;
      if (isMetricAlarm) {
        botSubtitle = "! ALARM !";
        botColor = themeColors.crit;
      } else if (isMetricWarn) {
        botSubtitle = (APP_STATE.language === 'de') ? "WARNUNG" : "WARNING";
        botColor = themeColors.warn;
      } else if (APP_STATE.cycleHeld) {
        botSubtitle = (APP_STATE.language === 'de') ? "FESTGEHALTEN" : "HOLD";
        botColor = themeColors.warn;
      } else {
        switch (curDef.id) {
          case 0: case 2: case 9: botSubtitle = APP_STATE.info.cpu_name; break;
          case 1: case 3: case 5: botSubtitle = APP_STATE.info.gpu_name; break;
          case 4: botSubtitle = APP_STATE.info.ram_name; break;
          case 6: botSubtitle = APP_STATE.info.ssd_name; break;
          case 7: botSubtitle = APP_STATE.info.cpu_name; break;
          case 8: botSubtitle = APP_STATE.info.net_name; break;
          default: botSubtitle = ""; break;
        }
      }
      drawCurvedText(cx, cy, botSubtitle, 58, 90, false, botColor, "bold 9px var(--font-sans)");
    }

    function renderAwaitingMetric(cx, cy, themeColors) {
      ctx.beginPath();
      ctx.arc(cx, cy, 104, 0, Math.PI * 2);
      ctx.strokeStyle = themeColors.track;
      ctx.lineWidth = 2;
      ctx.stroke();
      ctx.fillStyle = themeColors.text_secondary;
      ctx.font = "bold 9px var(--font-sans)";
      ctx.textAlign = "center";
      ctx.fillText("WAITING FOR", cx, cy - 5);
      ctx.fillText("DEVICE STATUS", cx, cy + 10);
    }

    /* Solid Filled Arc helper */
    function drawSolidArc(cx, cy, rInner, rOuter, startRad, endRad, color) {
      ctx.beginPath();
      ctx.arc(cx, cy, rOuter, startRad, endRad, false);
      ctx.arc(cx, cy, rInner, endRad, startRad, true);
      ctx.closePath();
      ctx.fillStyle = color;
      ctx.fill();
    }

    /* Single Horizontal Baseline Center Cluster */
    function drawCenterValueAndUnit(cx, cy, val, unit, valCol, unitCol) {
      ctx.save();
      const yBase = cy + 6;

      let valStr = "";
      if (val === null || val === undefined || !Number.isFinite(val)) {
        valStr = "--";
      } else if (unit === "GB" || unit === "Mb/s") {
        valStr = Number(val).toFixed(1);
      } else {
        valStr = Math.round(val).toString();
      }

      ctx.font = "bold 26px var(--font-sans)";
      const valW = ctx.measureText(valStr).width;

      const isCelsius = (unit === "°C");
      const gap = 4;

      if (isCelsius) {
        ctx.font = "bold 13px var(--font-sans)";
        const cW = ctx.measureText("C").width;
        const degR = 2;
        const degSpan = degR * 2 + 3;
        const totalW = valW + gap + degSpan + cW;
        const xStart = cx - (totalW / 2);

        // Draw Value
        ctx.font = "bold 26px var(--font-sans)";
        ctx.fillStyle = valCol;
        ctx.textAlign = "left";
        ctx.textBaseline = "alphabetic";
        ctx.fillText(valStr, xStart, yBase);

        // Draw Degree Circle
        const degCx = xStart + valW + gap + degR + 1;
        const degCy = yBase - 14;
        ctx.beginPath();
        ctx.arc(degCx, degCy, degR, 0, Math.PI * 2);
        ctx.strokeStyle = unitCol;
        ctx.lineWidth = 1.5;
        ctx.stroke();

        // Draw 'C'
        ctx.font = "bold 13px var(--font-sans)";
        ctx.fillStyle = unitCol;
        ctx.fillText("C", xStart + valW + gap + degSpan, yBase);

      } else {
        ctx.font = "bold 12px var(--font-sans)";
        const unitW = unit ? ctx.measureText(unit).width : 0;
        const totalW = valW + (unitW > 0 ? (gap + unitW) : 0);
        const xStart = cx - (totalW / 2);

        // Draw Value
        ctx.font = "bold 26px var(--font-sans)";
        ctx.fillStyle = valCol;
        ctx.textAlign = "left";
        ctx.textBaseline = "alphabetic";
        ctx.fillText(valStr, xStart, yBase);

        // Draw Unit
        if (unitW > 0) {
          ctx.font = "bold 12px var(--font-sans)";
          ctx.fillStyle = unitCol;
          ctx.fillText(unit, xStart + valW + gap, yBase);
        }
      }
      ctx.restore();
    }

    /* Curved Text with Natural Left-to-Right Reading */
    function drawCurvedText(cx, cy, text, radius, centerDeg, isTopArc, color, font) {
      if (!text || text.length === 0) return;
      ctx.save();
      ctx.font = font;
      ctx.fillStyle = color;
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";

      const chars = text.split("");
      const charWidths = chars.map(ch => ctx.measureText(ch).width + 1.2);
      const charAngles = charWidths.map(w => (w / radius));
      const totalAngle = charAngles.reduce((a, b) => a + b, 0);

      if (isTopArc) {
        // Top Arc (around 270°): text advances left (<270°) to right (>270°)
        let curAngle = (centerDeg * Math.PI / 180) - (totalAngle / 2);
        for (let i = 0; i < chars.length; i++) {
          const step = charAngles[i];
          const mid = curAngle + (step / 2);
          curAngle += step;

          if (chars[i] === " ") continue;
          const px = cx + Math.cos(mid) * radius;
          const py = cy + Math.sin(mid) * radius;
          const rot = mid + Math.PI / 2; // Upright facing outward

          ctx.save();
          ctx.translate(px, py);
          ctx.rotate(rot);
          ctx.fillText(chars[i], 0, 0);
          ctx.restore();
        }
      } else {
        // Bottom Arc (around 90°): text advances left (>90°) to right (<90°) to read Left to Right!
        let curAngle = (centerDeg * Math.PI / 180) + (totalAngle / 2);
        for (let i = 0; i < chars.length; i++) {
          const step = charAngles[i];
          const mid = curAngle - (step / 2);
          curAngle -= step;

          if (chars[i] === " ") continue;
          const px = cx + Math.cos(mid) * radius;
          const py = cy + Math.sin(mid) * radius;
          const rot = mid - Math.PI / 2; // Upright facing inward toward center

          ctx.save();
          ctx.translate(px, py);
          ctx.rotate(rot);
          ctx.fillText(chars[i], 0, 0);
          ctx.restore();
        }
      }
      ctx.restore();
    }

    /* Offline Mode Screen */
    function renderOfflineScreen(cx, cy, nowSec, themeColors) {
      const rCenter = 38;

      // Rotating Beacon Arc
      const rotDeg = (nowSec * 120.0) % 360;
      const rotRad = (rotDeg * Math.PI) / 180;
      ctx.beginPath();
      ctx.arc(cx, cy, rCenter + 6, rotRad, rotRad + (Math.PI / 2), false);
      ctx.arc(cx, cy, rCenter + 2, rotRad + (Math.PI / 2), rotRad, true);
      ctx.closePath();
      ctx.fillStyle = themeColors.crit;
      ctx.fill();

      // Solid Center Circle
      ctx.beginPath();
      ctx.arc(cx, cy, rCenter, 0, Math.PI * 2);
      ctx.fillStyle = themeColors.center_fill;
      ctx.fill();
      ctx.strokeStyle = themeColors.crit;
      ctx.lineWidth = 1.5;
      ctx.stroke();

      // Pulsing Disconnected PC Icon
      const pulse = 0.5 + 0.5 * Math.sin(nowSec * 5.0);
      const iconCol = (pulse > 0.3) ? themeColors.crit : themeColors.warn;
      drawPcDisconnectedIcon(cx, cy, iconCol);

      // Curved Text Top: "NO PC LINK" (or "KEIN PC")
      const topStr = (APP_STATE.language === 'de') ? "KEIN PC" : "NO PC LINK";
      drawCurvedText(cx, cy, topStr, 58, 270, true, themeColors.crit, "bold 9px var(--font-sans)");

      // Curved Text Bottom: "WAITING FOR PC" (or "WARTE AUF PC")
      const botStr = (APP_STATE.language === 'de') ? "WARTE AUF PC" : "WAITING FOR PC";
      drawCurvedText(cx, cy, botStr, 58, 90, false, themeColors.text_secondary, "bold 9px var(--font-sans)");
    }

    function drawPcDisconnectedIcon(cx, cy, color) {
      ctx.save();
      const mw = 32, mh = 20;
      const mx = cx - (mw / 2);
      const my = cy - (mh / 2) - 2;

      // Monitor Screen Frame
      ctx.strokeStyle = color;
      ctx.lineWidth = 1.5;
      ctx.strokeRect(mx, my, mw, mh);

      // Stand Neck & Base
      ctx.fillStyle = color;
      ctx.fillRect(cx - 1, my + mh, 2, 4);
      ctx.fillRect(cx - 8, my + mh + 4, 16, 2);

      // Diagonal Slash Line
      const sx1 = mx - 2, sy1 = my - 2;
      const sx2 = mx + mw + 2, sy2 = my + mh + 2;

      // Dark understroke to cleanly cut through monitor borders
      ctx.beginPath();
      ctx.moveTo(sx1, sy1);
      ctx.lineTo(sx2, sy2);
      ctx.strokeStyle = "#000000";
      ctx.lineWidth = 3.5;
      ctx.stroke();

      // Bright slash
      ctx.beginPath();
      ctx.moveTo(sx1, sy1);
      ctx.lineTo(sx2, sy2);
      ctx.strokeStyle = color;
      ctx.lineWidth = 1.8;
      ctx.stroke();
      ctx.restore();
    }

    /* ==========================================================================
     * Telemetry & Auto-Cycle Management
     * ========================================================================== */
