# /**
#  * SPDX-FileCopyrightText: Copyright (c) 2026 Ark Crew. All rights reserved.
#  * SPDX-License-Identifier: LicenseRef-Ark-Crew-NonCommercial-1.0
#  *
#  * ARK CREW LIMITED NON-COMMERCIAL LICENSE NOTICE
#  *
#  * This source code, together with its associated documentation, examples,
#  * configuration files, and related materials, is collectively referred to
#  * as the "Software".
#  *
#  * Subject to the complete terms set forth in the LICENSE file, Ark Crew
#  * grants you a limited, non-exclusive, non-transferable, and non-sublicensable
#  * right to access, reproduce, and modify the Software solely for personal
#  * study, classroom education, academic research, and non-commercial evaluation.
#  *
#  * Commercial use of the Software, in whole or in part, is strictly prohibited
#  * without prior written authorization from Ark Crew. Prohibited activities
#  * include, without limitation, sale, sublicensing, paid distribution, use in
#  * paid consulting or training, incorporation into any commercial product or
#  * service, and internal development intended for commercial deployment.
#  *
#  * Except for the limited rights expressly granted under the applicable
#  * License, no license or other right, whether express, implied, by estoppel,
#  * or otherwise, is granted under any copyright, patent, trademark, trade
#  * secret, mask work, or other intellectual property right belonging to
#  * Ark Crew or any third party.
#  *
#  * Delivery or disclosure of the Software does not convey permission to use
#  * the Ark Crew name, trademarks, logos, visual identity, or other branding,
#  * except where strictly necessary to preserve the original attribution.
#  *
#  * THE SOFTWARE IS PROVIDED "AS IS" AND "WITH ALL FAULTS", WITHOUT ANY
#  * REPRESENTATION OR WARRANTY OF ANY KIND, WHETHER EXPRESS, IMPLIED,
#  * STATUTORY, OR OTHERWISE, INCLUDING WARRANTIES OF MERCHANTABILITY,
#  * FITNESS FOR A PARTICULAR PURPOSE, TITLE, ACCURACY, RELIABILITY, AND
#  * NON-INFRINGEMENT.
#  *
#  * TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, ARK CREW SHALL NOT
#  * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
#  * PUNITIVE, OR CONSEQUENTIAL LOSS OR DAMAGE ARISING FROM OR RELATED TO
#  * THE SOFTWARE, ITS USE, OR ITS INABILITY TO BE USED.
#  *
#  * This notice shall be retained in all authorized copies or substantial
#  * portions of the Software. Removal, concealment, or unauthorized alteration
#  * of this notice is prohibited.
#  *
#  * See the LICENSE file in the root directory of this repository for the
#  * complete and controlling license terms.
#  */

"""Versioned UTF-8 JSONL server used by ARK CREW Studio."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
import threading
from typing import Any


STUDIO_ROOT = Path(__file__).resolve().parents[1]
TOOLS_ROOT = STUDIO_ROOT / "resources" / "tools"

from studio.brain.errors import BrainError
from studio.brain.registry import ToolRegistry
from studio.brain.sdk_check import SDKCheckService
from studio.brain.settings import HistoryStore, SettingsStore
from studio.brain.tasks import TaskEngine
from studio.brain.workspace import WorkspaceService
from studio.brain.artifacts import ArtifactService
from studio.brain.resource_locks import ResourceLockManager
from studio.brain.serial_service import SerialSessionService


def json_safe(value: Any) -> Any:
    if isinstance(value, str):
        return "".join("\ufffd" if 0xD800 <= ord(character) <= 0xDFFF else character for character in value)
    if isinstance(value, dict):
        return {json_safe(key): json_safe(item) for key, item in value.items()}
    if isinstance(value, list):
        return [json_safe(item) for item in value]
    if isinstance(value, tuple):
        return [json_safe(item) for item in value]
    return value


class BrainServer:
    def __init__(self, workspace_path: Path | None = None, settings_root: Path | None = None) -> None:
        self.output_lock = threading.Lock()
        self.output_stream = getattr(sys.stdout, "buffer", None)
        self.settings = SettingsStore(settings_root)
        self.workspace = WorkspaceService(self.settings, workspace_path)
        self.registry = ToolRegistry(self._tools_root())
        self.resource_locks = ResourceLockManager()
        self.artifacts = ArtifactService(self.workspace)
        self.sdk_check = SDKCheckService(self.workspace, self.registry, self.emit_event)
        self.tasks = TaskEngine(
            self.workspace, self.registry, HistoryStore(self.settings), self.settings, self.sdk_check, self.emit_event,
            self.resource_locks, self.artifacts,
        )
        self.serial = SerialSessionService(self.resource_locks, self.emit_event)

    def _tools_root(self) -> Path:
        return TOOLS_ROOT

    def _reload_registry(self) -> None:
        self.registry = ToolRegistry(self._tools_root())
        self.sdk_check.registry = self.registry
        self.tasks.registry = self.registry

    def write(self, value: dict[str, Any]) -> None:
        payload = (json.dumps(json_safe(value), ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")
        with self.output_lock:
            if self.output_stream is not None:
                self.output_stream.write(payload)
                self.output_stream.flush()
            else:
                sys.stdout.write(payload.decode("utf-8"))
                sys.stdout.flush()

    def emit_event(self, event: str, data: dict[str, Any]) -> None:
        self.write({"type": "event", "event": event, "data": data})

    def dispatch(self, method: str, params: dict[str, Any]) -> Any:
        if method == "workspace.open":
            result = self.workspace.open(params.get("path"), params.get("app"))
            self._reload_registry()
            self.sdk_check.invalidate("工作区或App已切换")
            self.emit_event("workspace.changed", result)
            self.sdk_check.start(force=True, mode="quick")
            return result
        if method == "workspace.snapshot":
            return self.workspace.snapshot()
        if method == "registry.list":
            self.registry.reload()
            return self.registry.list()
        if method == "sdk_check.start":
            if params.get("reloadRegistry"):
                self._reload_registry()
            if params.get("force"):
                self.sdk_check.invalidate(str(params.get("reason", "手动重新检查")))
            return self.sdk_check.start(
                force=bool(params.get("force")),
                mode=str(params.get("mode", "quick")),
            )
        if method == "sdk_check.snapshot":
            return self.sdk_check.snapshot()
        if method == "task.start":
            return self.tasks.start(str(params.get("toolId", "")), params.get("params") or {})
        if method == "task.cancel":
            return self.tasks.cancel(str(params.get("taskId", "")))
        if method == "task.list":
            return self.tasks.list()
        if method == "task.get":
            return self.tasks.get(str(params.get("taskId", "")))
        if method == "task.history.clear":
            return self.tasks.clear_history()
        if method == "serial.open":
            return self.serial.open(str(params.get("port", "")), int(params.get("baud", 115200)))
        if method == "serial.close":
            return self.serial.close()
        if method == "serial.write":
            return self.serial.write(str(params.get("dataHex", "")))
        if method == "serial.snapshot":
            return self.serial.snapshot()
        if method == "artifact.list":
            return self.artifacts.list(str(params.get("app", "")) or None)
        if method == "artifact.read":
            return self.artifacts.read(str(params.get("artifactId", "")), int(params.get("maxBytes", 1048576)))
        if method == "tool.params.get":
            root = self.workspace.require_root()
            tool_id = str(params.get("toolId", ""))
            tool = self.registry.get(tool_id)
            saved = self.settings.get_tool_params(str(root), self.workspace.active_app, tool_id)
            allowed = {item.name for item in tool.inputs}
            return {key: value for key, value in saved.items() if key in allowed}
        if method == "tool.params.update":
            root = self.workspace.require_root()
            tool = self.registry.get(str(params.get("toolId", "")))
            values = self.registry.validate_params(tool, params.get("values") or {})
            if tool.parameter_mode == "dynamic":
                remembered = {item.name for item in tool.inputs if item.remember}
                values = {key: value for key, value in values.items() if key in remembered}
            return self.settings.set_tool_params(str(root), self.workspace.active_app, tool.id, values)
        if method == "settings.get":
            return self.settings.get()
        if method == "settings.update":
            values = params.get("values")
            if not isinstance(values, dict):
                raise BrainError("INVALID_PARAMS", "settings values must be an object")
            result = self.settings.update(values)
            self.emit_event("settings.updated", result)
            return result
        if method == "ping":
            return {"ok": True, "protocolVersion": 2, "capabilities": ["serialSession", "artifacts"]}
        raise BrainError("UNKNOWN_METHOD", f"unknown method '{method}'")

    def run(self) -> int:
        self.emit_event("system.ready", {
            "protocolVersion": 2, "workspace": self.workspace.snapshot(), "sdkCheck": self.sdk_check.snapshot(),
        })
        if self.workspace.opened:
            self.sdk_check.start(force=True, mode="quick")
        for line in sys.stdin:
            request_id: Any = None
            try:
                request = json.loads(line)
                if not isinstance(request, dict):
                    raise BrainError("INVALID_REQUEST", "request must be an object")
                request_id = request.get("id")
                method = request.get("method")
                params = request.get("params", {})
                if not isinstance(request_id, str) or not isinstance(method, str) or not isinstance(params, dict):
                    raise BrainError("INVALID_REQUEST", "id/method/params have invalid types")
                result = self.dispatch(method, params)
                self.write({"type": "response", "id": request_id, "result": result})
            except BrainError as exc:
                self.write({"type": "response", "id": request_id, "error": {"code": exc.code, "message": str(exc)}})
            except (OSError, ValueError, json.JSONDecodeError) as exc:
                self.write({"type": "response", "id": request_id, "error": {"code": "REQUEST_FAILED", "message": str(exc)}})
        self.serial.close()
        return 0


def main() -> int:
    if hasattr(sys.stdin, "reconfigure"):
        sys.stdin.reconfigure(encoding="utf-8", errors="strict")
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="strict")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path)
    args = parser.parse_args()
    return BrainServer(args.workspace.resolve() if args.workspace else None).run()


if __name__ == "__main__":
    raise SystemExit(main())
