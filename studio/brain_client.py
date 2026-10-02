# SPDX-FileCopyrightText: Copyright (c) 2026 Ark Crew. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Ark-Crew-NonCommercial-1.0

"""Qt process adapter for the versioned Brain JSONL protocol."""

from __future__ import annotations

import json
import os
from pathlib import Path
import sys
from typing import Any, Callable

from PySide6.QtCore import QObject, QProcess, QProcessEnvironment, QTimer, Signal


Callback = Callable[[Any], None]


class BrainClient(QObject):
    event_received = Signal(str, object)
    backend_error = Signal(str)
    state_changed = Signal(bool)

    def __init__(self, workspace: Path | None = None) -> None:
        super().__init__()
        self.workspace = workspace
        self.process = QProcess(self)
        self.process.readyReadStandardOutput.connect(self._read_stdout)
        self.process.readyReadStandardError.connect(self._read_stderr)
        self.process.finished.connect(self._finished)
        self._buffer = bytearray()
        self._sequence = 0
        self._pending: dict[str, tuple[Callback, Callback, QTimer]] = {}
        self._stopping = False

    def start(self) -> None:
        if self.process.state() != QProcess.ProcessState.NotRunning:
            return
        self._stopping = False
        environment = QProcessEnvironment.systemEnvironment()
        environment.insert("PYTHONIOENCODING", "utf-8")
        environment.insert("PYTHONUTF8", "1")
        self.process.setProcessEnvironment(environment)
        if getattr(sys, "frozen", False):
            program = sys.executable
            arguments = ["--brain-worker"]
        else:
            program = sys.executable
            arguments = ["-u", "-m", "studio", "--brain-worker"]
        if self.workspace:
            arguments.extend(["--workspace", str(self.workspace)])
        self.process.setProgram(program)
        self.process.setArguments(arguments)
        self.process.setWorkingDirectory(str(Path(__file__).resolve().parents[1]))
        self.process.start()

    def stop(self) -> None:
        if self.process.state() == QProcess.ProcessState.NotRunning:
            return
        self._stopping = True
        self.request("serial.close", {}, lambda _value: None, lambda _error: None)
        self.process.closeWriteChannel()
        if not self.process.waitForFinished(800):
            self.process.kill()
            self.process.waitForFinished(1000)

    def request(
        self,
        method: str,
        params: dict[str, Any] | None = None,
        callback: Callback | None = None,
        error: Callback | None = None,
    ) -> str:
        self._sequence += 1
        request_id = f"qt-{self._sequence}"
        timer = QTimer(self)
        timer.setSingleShot(True)

        def timed_out() -> None:
            pending = self._pending.pop(request_id, None)
            if pending:
                pending[1](f"Brain请求超时：{method}")

        timer.timeout.connect(timed_out)
        self._pending[request_id] = (callback or (lambda _value: None), error or self.backend_error.emit, timer)
        document = json.dumps({"id": request_id, "method": method, "params": params or {}}, ensure_ascii=False)
        self.process.write((document + "\n").encode("utf-8"))
        timer.start(30_000)
        return request_id

    def _read_stdout(self) -> None:
        self._buffer.extend(bytes(self.process.readAllStandardOutput()))
        if self._stopping:
            self._buffer.clear()
            return
        while b"\n" in self._buffer:
            line, _, remainder = self._buffer.partition(b"\n")
            self._buffer = bytearray(remainder)
            if line.strip():
                try:
                    decoded = line.decode("utf-8", errors="strict")
                except UnicodeDecodeError as exc:
                    self.backend_error.emit(f"Brain输出不是UTF-8：{exc}")
                    continue
                self._handle_line(decoded)

    def _handle_line(self, line: str) -> None:
        try:
            message = json.loads(line)
        except ValueError:
            self.backend_error.emit(f"Brain输出不是有效JSON：{line}")
            return
        if message.get("type") == "event":
            self.event_received.emit(str(message.get("event", "")), message.get("data"))
            if message.get("event") == "system.ready":
                self.state_changed.emit(True)
            return
        request_id = message.get("id")
        pending = self._pending.pop(request_id, None)
        if pending is None:
            return
        callback, error, timer = pending
        timer.stop()
        if message.get("error"):
            error(str(message["error"].get("message", "Brain请求失败")))
        else:
            callback(message.get("result"))

    def _read_stderr(self) -> None:
        text = bytes(self.process.readAllStandardError()).decode("utf-8", errors="replace").strip()
        if text:
            self.backend_error.emit(text)

    def _finished(self) -> None:
        for callback, error, timer in self._pending.values():
            del callback
            timer.stop()
            error("Brain进程已退出")
        self._pending.clear()
        self.state_changed.emit(False)
