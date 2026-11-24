# esp-idf-system-monitor

Universal Hardware Telemetry, Peak Tracker, Circular Gauge Display Engine, and Alarm Pipeline for ESP-IDF.

---

## Features
- **GC9A01 240x240 Circular Display Engine**:
  - Pure black background (`0x0000`) with high-contrast typography.
  - 14 px thick, solid filled 270° outer gauge arc (`fillArc`).
  - Precision watchmaker radial scale ticks (20 ticks inside track, major every 5).
  - Curved circular text along circle arc (Top: Metric title, Bottom: Hardware name / status) with consistent Left-to-Right reading.
  - Dedicated offline screen with solid center circle, rotating beacon arc, and blinking PC disconnected monitor icon.
  - Multi-theme support (`Monochrome`, `Cyber Cyan`, `Industrial Amber`).
  - Localization (`English` default, `Deutsch`).
  - Dynamic resolution scaling architecture for round displays (240x240, 280x280, 360x360, 480x480).
- **Transport-Neutral Telemetry Engine**:
  - Parses `!pc_hw` and `!pc_hw_info` lines over serial (UART0).
  - Per-core tracking for up to 32 CPU cores.
  - Rolling 60-sample peak tracker.
  - Alarm pipeline with yellow warning, red critical pulse, fan stall detection, and 90s snooze/ack.
  - Watchdog offline detection after 3.0 seconds.

---

## Developer & End-User Tools

### 1. Web Serial Studio (No Python / Zero Installation)
Open [`tools-web/system_monitor_console.html`](tools-web/system_monitor_console.html) in Google Chrome, Microsoft Edge, Opera, or Brave:
- Direct serial connection to COM4 via Web Serial API.
- Virtual GC9A01 display simulator in real-time.
- Workload scenario switcher (`Idle`, `Gaming`, `Render`, `Warning`, `Heat Alarm`).
- Live hardware sliders for CPU/GPU temperatures, loads, fans, network, and RAM.
- One-click autonomous 13-point diagnostic test runner.
- Live serial console monitor with interactive command input.

### 2. Autonomous Diagnostics
- [`tests/test_system_monitor_autonomously.py`](tests/test_system_monitor_autonomously.py): End-to-end 13-point automated test suite with hardware RTS reset.

---

## Examples
- `examples/ws-esp32-p4-wifi6-gc9a01-console-system-monitor-dev`: Complete reference project for the Waveshare ESP32-P4-WiFi6 development board with 1.28" GC9A01 circular LCD.

---

## License

This project is licensed under the [PolyForm Noncommercial License 1.0.0](LICENSE).
- **Free for noncommercial use**: Personal, educational, research, and hobbyist projects are completely free.
- **Commercial use**: Not permitted without prior authorization. For commercial inquiries, please contact the author via GitHub.
