# SPDX-FileCopyrightText: Copyright (c) 2026 Ark Crew. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Ark-Crew-NonCommercial-1.0

"""Single-session serial transport exposed through the Brain protocol."""

from __future__ import annotations

import os
import threading
from typing import Any, Callable

from .errors import BrainError
from .models import utc_now
from .resource_locks import ResourceLockManager


EventSink = Callable[[str, dict[str, Any]], None]


class SerialSessionService:
    OWNER = "studio:serial"

    def __init__(self, locks: ResourceLockManager, event_sink: EventSink) -> None:
        self.locks = locks
        self.event_sink = event_sink
        self._serial: Any = None
        self._port = ""
        self._baud = 0
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._test_mode = os.environ.get("ARK_STUDIO_TEST_MODE") == "1"

    def snapshot(self) -> dict[str, Any]:
        return {
            "opened": self._serial is not None or (self._test_mode and bool(self._port)),
            "port": self._port,
            "baud": self._baud,
            "testMode": self._test_mode,
        }

    def open(self, port: str, baud: int) -> dict[str, Any]:
        if not port:
            raise BrainError("INVALID_SERIAL_PORT", "请选择串口")
        if not 300 <= baud <= 4_000_000:
            raise BrainError("INVALID_BAUD", "串口波特率必须在300到4000000之间")
        if self.snapshot()["opened"]:
            self.close()
        resource = (f"serial:{port.lower()}",)
        if not self.locks.acquire(self.OWNER, resource, wait=False):
            raise BrainError("RESOURCE_BUSY", f"串口 {port} 正被其他任务占用")
        try:
            self._port = port
            self._baud = baud
            self._stop.clear()
            if not self._test_mode:
                try:
                    import serial
                except ImportError as exc:
                    raise BrainError("PYSERIAL_MISSING", "缺少pyserial，无法打开串口") from exc
                self._serial = serial.Serial(port=port, baudrate=baud, timeout=0.1, write_timeout=1.0)
                self._thread = threading.Thread(target=self._read_loop, daemon=True)
                self._thread.start()
            self.event_sink("serial.opened", self.snapshot())
            return self.snapshot()
        except Exception:
            self._port = ""
            self._baud = 0
            self._serial = None
            self.locks.release(self.OWNER, resource)
            raise

    def write(self, data_hex: str) -> dict[str, Any]:
        if not self.snapshot()["opened"]:
            raise BrainError("SERIAL_NOT_OPEN", "串口尚未连接")
        try:
            payload = bytes.fromhex(data_hex)
        except ValueError as exc:
            raise BrainError("INVALID_SERIAL_DATA", "串口数据必须是十六进制字节") from exc
        if not payload:
            return {"written": 0}
        if self._test_mode:
            self.event_sink("serial.tx", {"timestamp": utc_now(), "dataHex": payload.hex(" ")})
            self.event_sink("serial.rx", {"timestamp": utc_now(), "dataHex": payload.hex(" ")})
            return {"written": len(payload)}
        assert self._serial is not None
        try:
            written = int(self._serial.write(payload))
        except OSError as exc:
            self._fail(str(exc))
            raise BrainError("SERIAL_WRITE_FAILED", str(exc)) from exc
        self.event_sink("serial.tx", {"timestamp": utc_now(), "dataHex": payload[:written].hex(" ")})
        return {"written": written}

    def close(self) -> dict[str, Any]:
        port = self._port
        if not port:
            return self.snapshot()
        self._stop.set()
        serial_port = self._serial
        self._serial = None
        if serial_port is not None:
            try:
                serial_port.close()
            except OSError:
                pass
        if self._thread is not None and self._thread is not threading.current_thread():
            self._thread.join(timeout=0.5)
        self._thread = None
        self.locks.release(self.OWNER, (f"serial:{port.lower()}",))
        self._port = ""
        self._baud = 0
        result = self.snapshot()
        self.event_sink("serial.closed", {**result, "port": port})
        return result

    def _read_loop(self) -> None:
        while not self._stop.is_set() and self._serial is not None:
            try:
                payload = self._serial.read(4096)
            except OSError as exc:
                self._fail(str(exc))
                return
            if payload:
                self.event_sink("serial.rx", {"timestamp": utc_now(), "dataHex": payload.hex(" ")})

    def _fail(self, message: str) -> None:
        self.event_sink("serial.error", {"timestamp": utc_now(), "message": message})
        self.close()
