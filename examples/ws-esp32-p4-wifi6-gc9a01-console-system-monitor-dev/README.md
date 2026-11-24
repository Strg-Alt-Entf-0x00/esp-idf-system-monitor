# System Monitor Console Development Example

This example runs the system-monitor component with an interactive UTF-8 UART
console. It accepts `!pc_hw` / `!pc_hw_info` packets, demonstrates telemetry
and alarms with sample packets, and exposes the component's public runtime
controls. The PC-side sensor collector and its transport are intentionally not
part of this ESP-IDF component; a host application on any operating system can
send the documented lines over a transport connected to the firmware.

## Build

From this directory, run:

```bat
esp-build.bat
```

The helper uses the ESP-IDF environment selected on the machine. To choose a
different target, activate ESP-IDF and run `idf.py set-target <target>` from
this example directory before building.

## Commands

Connect a serial terminal at **115200 baud** and enter `help`.

| Command | Purpose |
|---|---|
| `status` | All telemetry, core loads, hardware names, alarms, peaks, and configuration |
| `telemetry` | Every sensor field and per-core load, with sample age |
| `metrics` | List every selectable center metric and its index |
| `alarms` | Show critical and warning alarm flags |
| `peaks` | Peak values tracked over the latest 60 samples |
| `info` | Hardware names from the latest `!pc_hw_info` packet |
| `metric cpu-temp` | Select a center metric |
| `config` | Show alarm thresholds and auto-cycle interval |
| `config cpu-temp 85` | Set a threshold (`gpu-temp`, `ram-percent`, `fan-rpm`) |
| `config cycle-ms 4000` | Set auto-cycle interval; `0` disables it |
| `config defaults` | Restore example defaults (not saved across reboot) |
| `cycle hold\|run\|toggle` | Hold/release/toggle automatic metric cycling |
| `rotate right pressed` | Simulate encoder rotation while pressed |
| `click` / `click long` | Simulate encoder click or long press |
| `ack` | Snooze alarms for 90 seconds |
| `feed !pc_hw ...` | Pass a raw telemetry packet to the component |
| `!pc_hw ...` | Same as `feed`, without the command prefix |
| `!pc_hw_info ...` | Feed five pipe-delimited hardware names directly |
| `demo normal\|warning\|critical\|partial` | Inject representative telemetry, including unavailable sensor values |
| `demo-info` | Inject sample hardware names |
| `tick <ms>` | Advance auto-cycle time for testing |

Configuration changes apply immediately and are not persisted by this example.
The `tick <ms>` command advances the auto-cycle timer for testing; the PC
offline watchdog still uses real elapsed time. `status` and `telemetry` always
include every received core load, regardless of the currently selected metric.

## Host telemetry protocol

The parser consumes one UTF-8/ASCII-compatible line per packet. Field separators
are spaces (or tabs); terminate lines with LF or CRLF. The telemetry packet has
18 required tokens followed by an optional comma-separated core-load token:

```text
!pc_hw <sequence> <cpu_C> <gpu_C> <ssd_C> <cpu_load_pct> <gpu_load_pct> <ram_used_GB> <ram_total_GB> <vram_used_GB> <vram_total_GB> <cpu_fan_RPM> <case_fan_RPM> <gpu_fan_RPM> <cpu_power_W> <gpu_power_W> <network_down_Mbps> <network_up_Mbps> [core0_pct,core1_pct,...]
```

Example:

```text
!pc_hw 42 61.5 55.0 43.0 18.0 4.0 12.4 32.0 2.1 8.0 900 700 0 42.5 18.0 120.5 8.2 18.0,12.0,24.0,18.0
```

The sequence is an unsigned 32-bit integer and may wrap. Numeric fields use a
period as the decimal separator. Temperatures are degrees Celsius, utilization
is percent, memory is decimal GB as reported by the host, fan speed is RPM,
power is watts, and network throughput is Mbps. Sensor data that is unavailable
must be sent as the literal `nan`; it is accepted for sensor measurements and
fan RPM, but not for sequence numbers or core loads. Unavailable fan readings
are stored as `SYS_MON_FAN_RPM_UNAVAILABLE` and do not trigger the fan-stall
alarm. Core loads are optional; when present, provide 1 to 32 comma-separated percentages in
0..100. Omit the final token if per-core readings are unavailable.
CPU/GPU loads and core loads must be in 0..100; temperatures must be in
-100..250 C; memory, power, and throughput values must be non-negative.
A `fan-rpm` threshold of `0` disables the fan-stall alarm.

Hardware names use exactly five pipe-delimited fields in this order: CPU, GPU,
RAM, SSD, network. Empty fields mean “not reported”:

```text
!pc_hw_info Example CPU|Example GPU|32 GB DDR5|NVMe SSD||
```

Each name is limited to 63 bytes plus its terminator. Pipe characters are
reserved as separators and are not escaped. Packets are limited to 511 bytes
excluding the terminating NUL. The parser is transport-neutral; this example
receives lines over UART, while a host collector or other transport adapter
must be supplied separately.
