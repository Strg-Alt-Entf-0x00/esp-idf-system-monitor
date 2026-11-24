#!/usr/bin/env python3
"""
Autonomous End-to-End Test Suite for ESP32-P4 System Monitor & GC9A01 Display

Performs automated verification over Serial (COM4):
  1. Serial connectivity & prompt check
  2. Command list verification ('help')
  3. Hardware info protocol ('!pc_hw_info')
  4. Real-time telemetry parsing ('!pc_hw')
  5. Per-core load ingestion & reporting
  6. Metric selection ('metric <name>')
  7. Encoder navigation simulation ('rotate', 'click')
  8. Auto-cycle control ('cycle hold/run')
  9. Display theming ('display theme mono/cyan/amber')
 10. Display localization ('display lang de/en')
 11. Display brightness control ('display brightness')
 12. Alarm system verification (Normal -> Warning -> Critical -> Ack)
 13. Watchdog offline timeout (>3.0s) & online recovery
"""

import sys
import time
import re
import argparse

try:
    import serial
except ImportError:
    print("ERROR: pyserial is required. Run: pip install pyserial")
    sys.exit(1)


class Colors:
    GREEN = "\033[92m"
    RED = "\033[91m"
    YELLOW = "\033[93m"
    CYAN = "\033[96m"
    BOLD = "\033[1m"
    RESET = "\033[0m"


class SystemMonitorTester:
    def __init__(self, port="COM4", baud=115200, timeout=1.0):
        self.port = port
        self.baud = baud
        self.timeout = timeout
        self.ser = None
        self.passed_tests = 0
        self.failed_tests = 0
        self.test_results = []

    def connect(self):
        print(f"{Colors.CYAN}Connecting to ESP32 on {self.port} @ {self.baud} baud...{Colors.RESET}")
        self.ser = serial.Serial(self.port, self.baud, timeout=self.timeout)
        # Hardware reset via RTS to guarantee clean test state
        self.ser.dtr = False
        self.ser.rts = True
        time.sleep(0.1)
        self.ser.rts = False
        time.sleep(1.8)
        self.ser.reset_input_buffer()
        self.ser.reset_output_buffer()

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()

    def send_cmd(self, cmd, wait_s=0.3):
        self.ser.reset_input_buffer()
        cmd_bytes = (cmd.strip() + "\r\n").encode("utf-8")
        self.ser.write(cmd_bytes)
        time.sleep(wait_s)
        resp = ""
        while self.ser.in_waiting > 0:
            chunk = self.ser.read(self.ser.in_waiting).decode("utf-8", errors="ignore")
            resp += chunk
            time.sleep(0.05)
        return resp

    def record_result(self, name, passed, detail=""):
        if passed:
            self.passed_tests += 1
            print(f"  [{Colors.GREEN}PASS{Colors.RESET}] {name}")
        else:
            self.failed_tests += 1
            print(f"  [{Colors.RED}FAIL{Colors.RESET}] {name}: {detail}")
        self.test_results.append((name, passed, detail))

    def test_01_prompt(self):
        resp = self.send_cmd("")
        passed = "system-monitor>" in resp
        self.record_result("Console Prompt & Serial Handshake", passed, resp.strip())

    def test_02_help_command(self):
        resp = self.send_cmd("help")
        keywords = ["status", "telemetry", "metric", "config", "cycle", "rotate", "click", "display", "feed"]
        missing = [kw for kw in keywords if kw not in resp]
        passed = len(missing) == 0
        self.record_result("Help & Available Commands", passed, f"Missing: {missing}" if missing else "")

    def test_03_hardware_info(self):
        hw_packet = "!pc_hw_info AMD Ryzen 7 9800X3D|NVIDIA GeForce RTX 4080|64 GB DDR5-6000|Samsung 990 Pro 2TB|Intel 2.5 GbE"
        feed_resp = self.send_cmd(hw_packet)
        info_resp = self.send_cmd("info")
        passed = ("AMD Ryzen 7 9800X3D" in info_resp and 
                  "NVIDIA GeForce RTX 4080" in info_resp and 
                  "64 GB DDR5-6000" in info_resp and 
                  "Samsung 990 Pro 2TB" in info_resp and 
                  "Intel 2.5 GbE" in info_resp)
        self.record_result("Hardware Info Protocol (!pc_hw_info)", passed, info_resp.strip())

    def test_04_telemetry_packet(self):
        telem_packet = "!pc_hw 101 45.5 42.0 38.0 22.0 15.0 16.50 64.00 4.20 16.00 850 700 0 45.0 35.0 55.40 12.20 25.0,20.0,30.0,18.0"
        self.send_cmd(telem_packet)
        status_resp = self.send_cmd("status")
        
        passed = ("online" in status_resp and
                  "45.5" in status_resp and  # CPU temp
                  "42.0" in status_resp and  # GPU temp
                  "16.50" in status_resp and # RAM used
                  "850" in status_resp and   # CPU fan
                  "55.40" in status_resp)    # Net down
        self.record_result("Telemetry Packet Ingestion (!pc_hw)", passed, status_resp.strip())

    def test_05_per_core_reporting(self):
        status_resp = self.send_cmd("status")
        passed = "Per-core CPU load (4 cores): 0=25.0% 1=20.0% 2=30.0% 3=18.0%" in status_resp
        self.record_result("Per-Core Load Ingestion & Reporting", passed, status_resp.strip())

    def test_06_metric_selection(self):
        metrics_to_test = [
            ("gpu-temp", "GPU temperature"),
            ("cpu-load", "CPU load"),
            ("ram", "RAM"),
            ("fan-cpu", "CPU fan"),
            ("cores", "Core loads")
        ]
        all_passed = True
        failed_metric = ""
        for m_name, expected_text in metrics_to_test:
            resp = self.send_cmd(f"metric {m_name}")
            status = self.send_cmd("status")
            if f"Center metric: {m_name}" not in status:
                all_passed = False
                failed_metric = m_name
                break
        self.record_result("Center Metric Selection ('metric <name>')", all_passed, f"Failed on {failed_metric}")

    def test_07_encoder_simulation(self):
        self.send_cmd("metric cpu-temp")
        self.send_cmd("rotate right")
        status_right = self.send_cmd("status")
        is_gpu_temp = "Center metric: gpu-temp" in status_right
        
        self.send_cmd("rotate left")
        status_left = self.send_cmd("status")
        is_cpu_temp = "Center metric: cpu-temp" in status_left
        
        passed = is_gpu_temp and is_cpu_temp
        self.record_result("Rotary Encoder Simulation ('rotate left/right')", passed, f"R:{is_gpu_temp}, L:{is_cpu_temp}")

    def test_08_cycle_hold_run(self):
        self.send_cmd("cycle hold")
        status_held = self.send_cmd("status")
        held = "auto-cycle: held" in status_held
        
        self.send_cmd("cycle run")
        status_running = self.send_cmd("status")
        running = "auto-cycle: running" in status_running
        
        passed = held and running
        self.record_result("Auto-Cycle Hold & Run Control", passed, f"Held:{held}, Running:{running}")

    def test_09_display_theming(self):
        themes = ["cyan", "amber", "mono"]
        all_passed = True
        for th in themes:
            resp = self.send_cmd(f"display theme {th}")
            if "Display theme:" not in resp:
                all_passed = False
                break
        self.record_result("Display Theming ('display theme mono/cyan/amber')", all_passed)

    def test_10_display_localization(self):
        resp_en = self.send_cmd("display lang en")
        en_ok = "English" in resp_en
        resp_de = self.send_cmd("display lang de")
        de_ok = "Deutsch" in resp_de
        self.record_result("Display Multi-Language ('display lang de/en')", en_ok and de_ok)

    def test_11_display_brightness(self):
        resp_50 = self.send_cmd("display brightness 50")
        b50_ok = "50%" in resp_50
        resp_100 = self.send_cmd("display brightness 100")
        b100_ok = "100%" in resp_100
        self.record_result("Display Brightness Control ('display brightness')", b50_ok and b100_ok)

    def test_12_alarm_pipeline(self):
        # 1. Normal state
        self.send_cmd("demo normal")
        st_norm = self.send_cmd("status")
        norm_ok = "Critical: none" in st_norm

        # 2. Warning state
        self.send_cmd("demo warning")
        st_warn = self.send_cmd("status")
        warn_ok = "Warnings: CPU temperature" in st_warn or "Warnings:" in st_warn and "none" not in st_warn

        # 3. Critical state
        self.send_cmd("demo critical")
        st_crit = self.send_cmd("status")
        crit_ok = "Critical: CPU temperature" in st_crit or "Critical:" in st_crit and "none" not in st_crit

        # 4. Alarm Acknowledge
        resp_ack = self.send_cmd("ack")
        ack_ok = "Alarms snoozed" in resp_ack or "snoozed" in resp_ack

        passed = norm_ok and warn_ok and crit_ok and ack_ok
        self.record_result("Alarm Pipeline (Normal -> Warn -> Crit -> Ack)", passed, 
                           f"Norm:{norm_ok}, Warn:{warn_ok}, Crit:{crit_ok}, Ack:{ack_ok}")

    def test_13_watchdog_offline_recovery(self):
        print(f"    {Colors.YELLOW}Testing offline watchdog: waiting 5.5s without packets...{Colors.RESET}")
        time.sleep(5.5)
        st_offline = self.send_cmd("status")
        offline_detected = "PC: offline" in st_offline and "PC offline" in st_offline
        
        # Now send packet to test online recovery
        self.send_cmd("!pc_hw 201 40.0 38.0 35.0 10.0 5.0 12.0 64.0 2.0 16.0 600 500 0 25.0 20.0 1.0 0.2 10.0,10.0")
        st_online = self.send_cmd("status")
        online_recovered = "PC: online" in st_online
        
        passed = offline_detected and online_recovered
        self.record_result("Watchdog Offline Timeout (5.0s) & Online Recovery", passed, 
                           f"Offline:{offline_detected}, Recovered:{online_recovered}")

    def run_all(self):
        print(f"\n{Colors.BOLD}======================================================================{Colors.RESET}")
        print(f"{Colors.BOLD}   ESP32-P4 SYSTEM MONITOR & GC9A01 DISPLAY - AUTONOMOUS TEST SUITE    {Colors.RESET}")
        print(f"{Colors.BOLD}======================================================================\n{Colors.RESET}")
        
        try:
            self.connect()
            self.test_01_prompt()
            self.test_02_help_command()
            self.test_03_hardware_info()
            self.test_04_telemetry_packet()
            self.test_05_per_core_reporting()
            self.test_06_metric_selection()
            self.test_07_encoder_simulation()
            self.test_08_cycle_hold_run()
            self.test_09_display_theming()
            self.test_10_display_localization()
            self.test_11_display_brightness()
            self.test_12_alarm_pipeline()
            self.test_13_watchdog_offline_recovery()
        finally:
            self.close()

        total = self.passed_tests + self.failed_tests
        print(f"\n{Colors.BOLD}----------------------------------------------------------------------{Colors.RESET}")
        print(f"{Colors.BOLD}TEST SUMMARY:{Colors.RESET}")
        print(f"  Total tests:  {total}")
        print(f"  Passed:       {Colors.GREEN}{self.passed_tests}{Colors.RESET}")
        print(f"  Failed:       {Colors.RED if self.failed_tests > 0 else Colors.GREEN}{self.failed_tests}{Colors.RESET}")
        print(f"{Colors.BOLD}----------------------------------------------------------------------{Colors.RESET}")
        if self.failed_tests == 0:
            print(f"{Colors.GREEN}{Colors.BOLD}>>> ALL AUTONOMOUS TESTS PASSED SUCCESSFULLY! <<<{Colors.RESET}\n")
            return 0
        else:
            print(f"{Colors.RED}{Colors.BOLD}>>> SOME TESTS FAILED! <<<{Colors.RESET}\n")
            return 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Autonomous System Monitor Test Suite")
    parser.add_argument("--port", default="COM4", help="Serial port (default: COM4)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    args = parser.parse_args()

    tester = SystemMonitorTester(port=args.port, baud=args.baud)
    sys.exit(tester.run_all())
