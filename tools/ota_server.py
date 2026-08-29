#!/usr/bin/env python3
"""Minimal LAN OTA server for the ESP32-S3 test firmware."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


def version_tuple(value: str) -> tuple[int, int, int]:
    match = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", value.strip())
    if not match:
        raise ValueError(f"version must use x.y.z format: {value!r}")
    return tuple(int(part) for part in match.groups())


class OtaServer(ThreadingHTTPServer):
    firmware_path: Path
    firmware_version: str
    public_host: str
    firmware_sha256: str


class OtaHandler(BaseHTTPRequestHandler):
    server: OtaServer

    def log_message(self, fmt: str, *args: object) -> None:
        print(f"[OTA_SERVER] {self.client_address[0]} {fmt % args}", flush=True)

    def send_json(self, payload: dict[str, object], status: HTTPStatus = HTTPStatus.OK) -> None:
        body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        if self.path == "/health":
            firmware = self.server.firmware_path
            self.send_json(
                {
                    "status": "ok",
                    "version": self.server.firmware_version,
                    "firmware_exists": firmware.is_file(),
                    "firmware": str(firmware),
                }
            )
            return

        if self.path != "/firmware.bin":
            self.send_error(HTTPStatus.NOT_FOUND)
            return

        firmware = self.server.firmware_path
        if not firmware.is_file():
            self.send_error(HTTPStatus.NOT_FOUND, "firmware binary is not built yet")
            return

        size = firmware.stat().st_size
        print(
            f"[OTA_SERVER] Sending {firmware.name}, version={self.server.firmware_version}, "
            f"size={size}, sha256={self.server.firmware_sha256}",
            flush=True,
        )
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(size))
        self.send_header("X-Firmware-Version", self.server.firmware_version)
        self.send_header("X-Firmware-SHA256", self.server.firmware_sha256)
        self.end_headers()
        with firmware.open("rb") as source:
            while chunk := source.read(64 * 1024):
                self.wfile.write(chunk)

    def do_POST(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        if self.path != "/ota/check-update":
            self.send_error(HTTPStatus.NOT_FOUND)
            return

        try:
            content_length = int(self.headers.get("Content-Length", "0"))
            if content_length <= 0 or content_length > 4096:
                raise ValueError("invalid Content-Length")
            request = json.loads(self.rfile.read(content_length).decode("utf-8"))
            current_version = str(request["firmware_version"])
            current_tuple = version_tuple(current_version)
        except (KeyError, ValueError, json.JSONDecodeError, UnicodeDecodeError) as exc:
            self.send_json({"status": "error", "message": str(exc)}, HTTPStatus.BAD_REQUEST)
            return

        firmware = self.server.firmware_path
        has_update = firmware.is_file() and version_tuple(self.server.firmware_version) > current_tuple
        print(
            f"[OTA_SERVER] Check device={request.get('device_id')} model={request.get('model')} "
            f"current={current_version} latest={self.server.firmware_version} update={has_update}",
            flush=True,
        )

        payload: dict[str, object] = {
            "status": "success",
            "has_update": has_update,
            "latest_version": self.server.firmware_version,
        }
        if has_update:
            payload.update(
                {
                    "download_url": f"http://{self.server.public_host}:{self.server.server_port}/firmware.bin",
                    "size": firmware.stat().st_size,
                    "sha256": self.server.firmware_sha256,
                }
            )
        self.send_json(payload)


def main() -> None:
    project_dir = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Serve an ESP-IDF application binary over the LAN")
    parser.add_argument("--bind", default="0.0.0.0", help="local bind address")
    parser.add_argument("--host-ip", default="192.168.1.3", help="IP placed in the firmware URL")
    parser.add_argument("--port", type=int, default=8070)
    parser.add_argument("--version", default="1.0.1")
    parser.add_argument(
        "--firmware",
        type=Path,
        default=project_dir / "build" / "EVT_ESP32-S3.bin",
    )
    args = parser.parse_args()
    version_tuple(args.version)

    firmware = args.firmware.resolve()
    digest = ""
    if firmware.is_file():
        digest = hashlib.sha256(firmware.read_bytes()).hexdigest()

    server = OtaServer((args.bind, args.port), OtaHandler)
    server.firmware_path = firmware
    server.firmware_version = args.version
    server.public_host = args.host_ip
    server.firmware_sha256 = digest

    print(f"[OTA_SERVER] Listening on http://{args.host_ip}:{args.port}", flush=True)
    print(f"[OTA_SERVER] Health: http://{args.host_ip}:{args.port}/health", flush=True)
    print(f"[OTA_SERVER] Firmware: {firmware}", flush=True)
    if not firmware.is_file():
        print("[OTA_SERVER] WARNING: firmware does not exist; update checks will return false", flush=True)
    else:
        print(
            f"[OTA_SERVER] version={args.version} size={firmware.stat().st_size} sha256={digest}",
            flush=True,
        )

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[OTA_SERVER] Stopped", flush=True)
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
