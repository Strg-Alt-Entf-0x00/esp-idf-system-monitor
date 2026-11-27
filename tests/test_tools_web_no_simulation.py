import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
WEB = ROOT / "tools-web"


class ToolsWebRealTelemetryTests(unittest.TestCase):
    def test_web_ui_has_no_simulated_telemetry_controls(self):
        text = "\n".join(
            path.read_text(encoding="utf-8")
            for path in (WEB.glob("*.html"))
        ) + "\n".join(
            path.read_text(encoding="utf-8")
            for path in (WEB / "js").glob("*.js")
        )
        forbidden = (
            "SIMULATOR ACTIVE",
            "simulateFirmwareResponse",
            "applyDemoScenario",
            "onSensorSliderChange",
            "Live Telemetry Injection",
        )
        for value in forbidden:
            self.assertNotIn(value, text)

    def test_initial_dashboard_does_not_define_fake_sensor_values(self):
        state = (WEB / "js" / "state.js").read_text(encoding="utf-8")
        self.assertIn("cpu_temp: null", state)
        self.assertIn("fan_cpu: null", state)
        self.assertIn("cpu_name: \"\"", state)

    def test_device_controls_wait_for_reported_state(self):
        state = (WEB / "js" / "state.js").read_text(encoding="utf-8")
        controls = (WEB / "js" / "telemetry-controls.js").read_text(encoding="utf-8")
        serial = (WEB / "js" / "serial-terminal.js").read_text(encoding="utf-8")
        self.assertIn("activeMetric: null", state)
        self.assertIn("cycleHeld: null", state)
        self.assertIn("cycleIntervalMs: null", state)
        self.assertIn("pcOnline: null", state)
        self.assertIn('id="pcLinkStatus"', (WEB / "index.html").read_text(encoding="utf-8"))
        self.assertNotIn("checkAutoCycle", controls)
        self.assertIn("config cycle-ms", controls)
        self.assertIn("clearTelemetrySample", serial)
        self.assertIn(r"/^PC:\s*(online|offline)\s*\|/i", serial)
        self.assertIn("Automatic metric cycle is (held|running)", serial)
        self.assertIn("auto-cycle\\s+(\\d+)", serial)

    def test_packet_parser_handles_nan_and_hardware_info(self):
        serial = (WEB / "js" / "serial-terminal.js").read_text(encoding="utf-8")
        self.assertIn("line.startsWith(\"!pc_hw \")", serial)
        self.assertIn("line.startsWith(\"!pc_hw_info \")", serial)
        self.assertIn("parseOptionalFloat", serial)
        self.assertIn("parts.length === 18 || parts.length === 19", serial)

    def test_serial_console_matches_shared_console_controls(self):
        html = (WEB / "index.html").read_text(encoding="utf-8")
        serial = (WEB / "js" / "serial-terminal.js").read_text(encoding="utf-8")
        self.assertIn("Serial Console · 115200 baud", html)
        self.assertNotIn("sysmon&gt;", html)
        self.assertNotIn("term-prompt", html)
        self.assertIn('aria-label="Toggle Serial Console"', html)
        self.assertIn('aria-expanded="false"', html)
        self.assertIn('placeholder="Type a command and press Enter..."', html)
        self.assertIn('aria-label="Serial command"', html)
        for element_id in ("btnCopyTerm", "btnDownloadTerm", "btnClearTerm", "btnCloseDrawer"):
            self.assertIn(f'id="{element_id}"', html)
        self.assertIn("function readTerminalLog()", serial)
        self.assertIn("await copyTextToClipboard(readTerminalLog())", serial)
        self.assertIn("function downloadTerminalLog()", serial)
        self.assertIn("system-monitor-serial-console-log.txt", serial)
        self.assertNotIn("Terminal buffer cleared.", serial)


if __name__ == "__main__":
    unittest.main()
