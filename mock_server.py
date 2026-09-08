#!/usr/bin/env python3
"""
Local mock server for SuperGreenOS html app.

Usage:
  python mock_server.py
  python mock_server.py --port 8081 --root spiffs_fs
"""

from __future__ import annotations

import argparse
import json
import mimetypes
import pathlib
import random
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, HTTPServer


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser()
  parser.add_argument("--port", type=int, default=8080)
  parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path("spiffs_fs"))
  parser.add_argument("--legacy-dash", action="store_true", help="answer 404 on /dash and /kv like firmwares before 2026-09-07/08")
  return parser.parse_args()


def build_mock_values(config: dict) -> tuple[dict[str, str], dict[str, str]]:
  int_values: dict[str, str] = {}
  str_values: dict[str, str] = {}

  for key in config.get("keys", []):
    caps = key.get("caps_name", "")
    if not caps:
      continue
    if key.get("type") == "integer":
      int_values[caps] = "0"
    else:
      str_values[caps] = ""

  # Helpful defaults for header/debug
  int_values["WIFI_STATUS"] = "3"
  int_values["OTA_TIMESTAMP"] = "20260303"
  str_values["WIFI_IP"] = "192.168.1.57"

  # A plausible grow box so the dashboard tab has something to show
  # (values mirror the units of the real controller: VPD is kPa * 100).
  now = int(time.time())
  int_values.update({
    "TIME": str(now),
    "BOX_0_ENABLED": "1",
    "BOX_0_TEMP": "24",
    "BOX_0_HUMI": "58",
    "BOX_0_VPD": "116",
    "BOX_0_CO2": "0",
    "BOX_0_TIMER_TYPE": "1",
    "BOX_0_TIMER_OUTPUT": "100",
    "BOX_0_ON_HOUR": "8",
    "BOX_0_OFF_HOUR": "19",
    "BOX_0_LED_DIM": "80",
    "BOX_0_FAN_DUTY": "45",
    "BOX_0_FAN_REF_SOURCE": "8",
    "BOX_0_FAN_REF": "100",
    "BOX_0_FAN_REF_MIN": "0",
    "BOX_0_FAN_REF_MAX": "100",
    "BOX_0_BLOWER_DUTY": "30",
    "BOX_0_BLOWER_REF_SOURCE": "15",
    "BOX_0_BLOWER_REF": "58",
    "BOX_0_BLOWER_REF_MIN": "50",
    "BOX_0_BLOWER_REF_MAX": "70",
    "BOX_0_WATERING_POWER": "20",
    "BOX_0_WATERING_LEFT": "-1",
    "BOX_0_WATERING_PERIOD": "2880",
    "BOX_0_WATERING_DURATION": "20",
    "BOX_0_WATERING_LAST": str(now - 3600),
    "BOX_0_STARTED_AT": str(now - 12 * 86400),
    "BOX_0_DURATION_DAYS": "215",
    "LED_0_BOX": "0",
    "LED_0_DUTY": "80",
    "LED_0_DIM": "100",
    "LED_1_BOX": "0",
    "LED_1_DUTY": "80",
    "LED_1_DIM": "100",
    "SENSOR_HEALTH_ENABLED": "1",
    "SENSOR_HEALTH_PERIOD_S": "60",
    "SENSOR_HEALTH_WARMUP_SAMPLES": "3",
    "SENSOR_HEALTH_STUCK_SAMPLES": "5",
    "SENSOR_HEALTH_STATUS": "3",
  })
  str_values["SENSOR_HEALTH_LAST_ALERT"] = "box_0_temp_stuck"

  return int_values, str_values


START_TIME = int(time.time())

DASH_BOX_FIELDS = (
  "enabled", "temp", "humi", "vpd", "co2", "weight", "led_dim", "started_at", "duration_days",
  "timer_type", "timer_output", "on_hour", "on_min", "off_hour", "off_min",
  "fan_duty", "fan_ref", "fan_ref_min", "fan_ref_max", "fan_ref_source",
  "blower_duty", "blower_ref", "blower_ref_min", "blower_ref_max", "blower_ref_source",
  "watering_power", "watering_left", "watering_last", "watering_period", "watering_duration",
)


def build_mock_dash(int_values: dict[str, str], str_values: dict[str, str]) -> dict:
  """Same shape as GET /dash on the 2026-09-07 firmware."""
  def iv(key: str) -> int:
    value = int_values.get(key, "0")
    if key in ("BOX_0_TEMP", "BOX_0_HUMI", "BOX_0_VPD"):
      return int(value) + random.choice((-1, 0, 0, 1))
    return int(value)

  boxes = []
  for i in range(3):
    box = {"i": i}
    for field in DASH_BOX_FIELDS:
      box[field] = iv(f"BOX_{i}_{field.upper()}")
    boxes.append(box)
  leds = [{"box": iv(f"LED_{i}_BOX"), "duty": iv(f"LED_{i}_DUTY"), "dim": iv(f"LED_{i}_DIM")} for i in range(6)]
  return {
    "boxes": boxes,
    "leds": leds,
    "sensor_health": {
      "status": iv("SENSOR_HEALTH_STATUS"),
      "last_alert": str_values.get("SENSOR_HEALTH_LAST_ALERT", ""),
      "enabled": iv("SENSOR_HEALTH_ENABLED"),
      "period_s": iv("SENSOR_HEALTH_PERIOD_S"),
      "warmup_samples": iv("SENSOR_HEALTH_WARMUP_SAMPLES"),
      "stuck_samples": iv("SENSOR_HEALTH_STUCK_SAMPLES"),
    },
    "time": int(time.time()),
  }


def build_mock_diag(int_values: dict[str, str]) -> dict:
  return {
    "mqtt_stage": 6, "mqtt_disc_idx": 23, "state": 2, "wifi_status": 3, "mqtt_connected": 1,
    "n_restarts": 145, "ota_status": 0, "reset_reason": 3, "reset_history": "3,1",
    "heap_free": 40768, "heap_min_free": 2908, "heap_min_free_at": 31, "heap_low_events": 1,
    "uptime_s": int(time.time()) - START_TIME, "nvs_used": 293, "nvs_free": 211,
    "mqtt_stack_hwm": 6852, "time_valid": 1,
    "broker_url": "mqtt://sink2.supergreenlab.com:1883", "broker_clientid": "mock",
  }


class MockHandler(BaseHTTPRequestHandler):
  root: pathlib.Path
  config_raw: bytes
  int_values: dict[str, str]
  str_values: dict[str, str]
  legacy_dash: bool = False

  def _send(self, code: int, body: bytes, ctype: str = "text/plain") -> None:
    self.send_response(code)
    self.send_header("Content-Type", ctype)
    self.send_header("Content-Length", str(len(body)))
    self.end_headers()
    self.wfile.write(body)

  def _send_text(self, code: int, text: str, ctype: str = "text/plain") -> None:
    self._send(code, text.encode("utf-8"), ctype)

  def _file_response(self, file_path: pathlib.Path) -> None:
    if not file_path.exists() or not file_path.is_file():
      self._send_text(404, "not found")
      return
    ctype, _ = mimetypes.guess_type(str(file_path))
    self._send(200, file_path.read_bytes(), ctype or "application/octet-stream")

  def do_GET(self) -> None:
    parsed = urllib.parse.urlparse(self.path)
    path = parsed.path
    params = urllib.parse.parse_qs(parsed.query)

    if path in ("/", "/app.html", "/fs/app.html"):
      self._file_response(self.root / "app.html")
      return

    if path in ("/config.json", "/fs/config.json"):
      self._send(200, self.config_raw, "application/json")
      return

    if path == "/mqttdiag":
      self._send_text(200, json.dumps(build_mock_diag(self.int_values)), "application/json")
      return

    if path == "/dash":
      if MockHandler.legacy_dash:
        self._send_text(404, "This URI does not exist")
        return
      self._send_text(200, json.dumps(build_mock_dash(self.int_values, self.str_values)), "application/json")
      return

    if path == "/kv":
      if MockHandler.legacy_dash:
        self._send_text(404, "This URI does not exist")
        return
      ints = {k: int(v) for k, v in self.int_values.items()}
      strs = {k: v for k, v in self.str_values.items() if "PASSWORD" not in k}
      self._send_text(200, json.dumps({"i": ints, "s": strs}), "application/json")
      return

    if path == "/i":
      key = params.get("k", [""])[0]
      value = self.int_values.get(key, "0")
      # a little jitter so the dashboard sparklines have something to draw
      if key in ("BOX_0_TEMP", "BOX_0_HUMI", "BOX_0_VPD"):
        value = str(int(value) + random.choice((-1, 0, 0, 1)))
      self._send_text(200, value)
      return

    if path == "/s":
      key = params.get("k", [""])[0]
      self._send_text(200, self.str_values.get(key, ""))
      return

    self._send_text(404, "not found")

  def do_POST(self) -> None:
    parsed = urllib.parse.urlparse(self.path)
    params = urllib.parse.parse_qs(parsed.query)
    key = params.get("k", [""])[0]
    value = params.get("v", [""])[0]
    if parsed.path == "/i" and key:
      self.int_values[key] = str(int(value)) if value.lstrip("-").isdigit() else "0"
      self._send_text(200, "OK")
      return
    if parsed.path == "/s" and key:
      self.str_values[key] = value
      self._send_text(200, "OK")
      return
    if parsed.path in ("/i", "/s", "/signing"):
      self._send_text(200, "OK")
      return
    self._send_text(404, "not found")

  def log_message(self, fmt: str, *args) -> None:  # noqa: A003
    print(f"[mock] {self.address_string()} {fmt % args}")


def main() -> None:
  args = parse_args()
  root = args.root.resolve()
  config_path = root / "config.json"

  if not config_path.exists():
    raise SystemExit(f"Missing config file: {config_path}")

  config_obj = json.loads(config_path.read_text(encoding="utf-8"))
  int_values, str_values = build_mock_values(config_obj)

  MockHandler.root = root
  MockHandler.config_raw = config_path.read_bytes()
  MockHandler.int_values = int_values
  MockHandler.str_values = str_values
  MockHandler.legacy_dash = args.legacy_dash

  server = HTTPServer(("127.0.0.1", args.port), MockHandler)
  print(f"Mock server listening on http://127.0.0.1:{args.port} (root={root})")
  print("Press Ctrl+C to stop.")
  server.serve_forever()


if __name__ == "__main__":
  main()
