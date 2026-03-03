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
import urllib.parse
from http.server import BaseHTTPRequestHandler, HTTPServer


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser()
  parser.add_argument("--port", type=int, default=8080)
  parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path("spiffs_fs"))
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

  return int_values, str_values


class MockHandler(BaseHTTPRequestHandler):
  root: pathlib.Path
  config_raw: bytes
  int_values: dict[str, str]
  str_values: dict[str, str]

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

    if path == "/i":
      key = params.get("k", [""])[0]
      self._send_text(200, self.int_values.get(key, "0"))
      return

    if path == "/s":
      key = params.get("k", [""])[0]
      self._send_text(200, self.str_values.get(key, ""))
      return

    self._send_text(404, "not found")

  def do_POST(self) -> None:
    parsed = urllib.parse.urlparse(self.path)
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

  server = HTTPServer(("127.0.0.1", args.port), MockHandler)
  print(f"Mock server listening on http://127.0.0.1:{args.port} (root={root})")
  print("Press Ctrl+C to stop.")
  server.serve_forever()


if __name__ == "__main__":
  main()
