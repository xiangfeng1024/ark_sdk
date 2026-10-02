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

"""Workspace-wide SDK environment and tool availability inspection."""

from __future__ import annotations

from pathlib import Path
import os
import platform
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from typing import Any, Callable
import xml.etree.ElementTree as ET

from studio.tooling.keil_tools import find_uv4
from studio.tooling.probe_check import check_probe
from studio.tooling.project_config import detect_synced_app, load_app_config
from studio.tooling.ark_dts import generate_for_app
from studio.tooling.process_runner import hidden_process_kwargs

from .errors import BrainError
from .models import ToolSpec, utc_now
from .registry import ToolRegistry
from .workspace import WorkspaceService


EventSink = Callable[[str, dict[str, Any]], None]


class SDKCheckService:
    def __init__(self, workspace: WorkspaceService, registry: ToolRegistry, event_sink: EventSink) -> None:
        self.workspace = workspace
        self.registry = registry
        self.event_sink = event_sink
        self._lock = threading.RLock()
        self._running = False
        self._rerun = False
        self._rerun_mode = "quick"
        self._generation = 0
        self._resolved_uv4: Path | None = None
        self._probe_result: tuple[str, str] | None = None
        self._snapshot: dict[str, Any] = self._empty_snapshot()

    @staticmethod
    def _empty_snapshot() -> dict[str, Any]:
        return {
            "state": "idle", "stale": True, "mode": "quick", "durationMs": 0,
            "percent": 0, "completed": 0, "total": 0,
            "startedAt": None, "finishedAt": None, "workspace": "", "app": "",
            "items": [], "logs": [], "tools": {},
        }

    def snapshot(self) -> dict[str, Any]:
        with self._lock:
            result = dict(self._snapshot)
            result["items"] = [dict(item) for item in self._snapshot["items"]]
            result["logs"] = [dict(item) for item in self._snapshot["logs"]]
            result["tools"] = {key: dict(value) for key, value in self._snapshot["tools"].items()}
            return result

    def invalidate(self, reason: str = "工作区内容已变化", auto_start: bool = False) -> dict[str, Any]:
        with self._lock:
            self._generation += 1
            self._resolved_uv4 = None
            self._probe_result = None
            self._snapshot["stale"] = True
            if self._snapshot["state"] != "running":
                self._snapshot["state"] = "stale"
        self.event_sink("sdk_check.stale", {"reason": reason, "snapshot": self.snapshot()})
        if auto_start and self.workspace.opened:
            return self.start(force=True)
        return self.snapshot()

    def start(self, force: bool = False, mode: str = "quick") -> dict[str, Any]:
        self.workspace.require_root()
        if mode not in ("quick", "deep"):
            raise BrainError("INVALID_PARAMS", f"unknown SDK check mode '{mode}'")
        with self._lock:
            if self._running:
                if force:
                    self._rerun = True
                    self._rerun_mode = mode
                return self.snapshot()
            if not force and self._snapshot["state"] == "succeeded" and not self._snapshot["stale"]:
                return self.snapshot()
            self._running = True
            self._generation += 1
            self._resolved_uv4 = None
            self._probe_result = None
            generation = self._generation
        threading.Thread(target=self._run, args=(generation, mode), daemon=True).start()
        return self.snapshot()

    def tool_available(self, tool_id: str) -> tuple[bool, str]:
        with self._lock:
            if self._snapshot["state"] != "succeeded" or self._snapshot["stale"]:
                return False, "SDK环境检查尚未完成或结果已过期"
            value = self._snapshot["tools"].get(tool_id)
            if not value:
                return False, "工具尚未完成SDK检查"
            return bool(value.get("available")), str(value.get("message", ""))

    def require_uv4(self) -> Path:
        """Return the exact UV4 executable established by the current SDK check."""
        with self._lock:
            if self._snapshot["state"] != "succeeded" or self._snapshot["stale"]:
                raise BrainError("SDK_CHECK_REQUIRED", "SDK环境检查尚未完成或结果已过期")
            value = self._resolved_uv4
        if value is None or not value.is_file():
            raise BrainError("KEIL_NOT_FOUND", "SDK环境检查未找到可用的Keil UV4.exe")
        return value

    def _emit_log(self, level: str, source: str, text: str) -> None:
        with self._lock:
            timestamp = utc_now() if bool(self.workspace.settings.get().get("log_timestamps", True)) else ""
            entry = {
                "sequence": len(self._snapshot["logs"]) + 1, "timestamp": timestamp,
                "level": level, "source": source, "text": text,
            }
            self._snapshot["logs"].append(entry)
        self.event_sink("sdk_check.log", entry)

    def _run(self, generation: int, mode: str) -> None:
        check_started = time.monotonic()
        try:
            root = self.workspace.require_root()
            snapshot = self.workspace.snapshot()
            tools = [self.registry.get(item["id"]) for item in self.registry.list()]
            base_checks = [
                ("workspace", "SDK工作区", self._check_workspace),
                ("python", "Python运行环境", self._check_python_deep if mode == "deep" else self._check_python),
                ("keil", "Keil uVision", self._check_keil_deep if mode == "deep" else self._check_keil),
                ("project", "Keil工程", self._check_project_deep if mode == "deep" else self._check_project),
                ("dts", "DTS配置", self._check_dts),
                ("synced", "Keil App同步", self._check_synced),
                ("serial", "串口设备", self._check_serial),
                ("probe", "下载探针", self._check_probe_environment),
            ]
            if mode == "deep":
                base_checks.append(("dtc", "DTC兼容性", self._check_dtc))
            total = len(base_checks) + len(tools)
            with self._lock:
                self._snapshot = {
                    "state": "running", "stale": False, "mode": mode, "durationMs": 0,
                    "percent": 0, "completed": 0, "total": total,
                    "startedAt": utc_now(), "finishedAt": None, "workspace": str(root),
                    "app": snapshot["activeApp"], "items": [], "logs": [], "tools": {},
                }
            self.event_sink("sdk_check.started", self.snapshot())
            for check_id, title, callback in base_checks:
                self._run_item(check_id, title, "environment", None, callback, total, mode)
            tool_check_cache: dict[tuple[str, str], tuple[tuple[str, str], int]] = {}
            for tool in tools:
                cache_key = (tool.availability, str(tool.check.get("handler", "")))
                if cache_key in tool_check_cache:
                    result, _original_duration = tool_check_cache[cache_key]
                    duration_ms = 0
                else:
                    result, duration_ms = self._timed(lambda tool=tool: self._check_tool(tool, mode))
                    tool_check_cache[cache_key] = (result, duration_ms)
                status, message = result
                self._append_item(
                    f"tool:{tool.id}", tool.title, "tool", tool.id,
                    (status, message), total, duration_ms, mode,
                )
                with self._lock:
                    self._snapshot["tools"][tool.id] = {
                        "available": status != "error", "status": status, "message": message,
                        "durationMs": duration_ms, "depth": mode,
                    }
            with self._lock:
                if generation != self._generation:
                    self._snapshot["stale"] = True
                self._snapshot["state"] = "succeeded"
                self._snapshot["percent"] = 100
                self._snapshot["durationMs"] = max(0, round((time.monotonic() - check_started) * 1000))
                self._snapshot["finishedAt"] = utc_now()
            self._emit_log("success", "sdk_check", "SDK环境检查完成")
            self.event_sink("sdk_check.finished", self.snapshot())
        except Exception as exc:
            with self._lock:
                self._snapshot["state"] = "failed"
                self._snapshot["durationMs"] = max(0, round((time.monotonic() - check_started) * 1000))
                self._snapshot["finishedAt"] = utc_now()
            self._emit_log("error", "sdk_check", f"{type(exc).__name__}: {exc}")
            self.event_sink("sdk_check.finished", self.snapshot())
        finally:
            with self._lock:
                self._running = False
                rerun = self._rerun
                rerun_mode = self._rerun_mode
                self._rerun = False
                self._rerun_mode = "quick"
            if rerun and self.workspace.opened:
                self.start(force=True, mode=rerun_mode)

    @staticmethod
    def _timed(callback: Callable[[], tuple[str, str]]) -> tuple[tuple[str, str], int]:
        started = time.monotonic()
        result = callback()
        return result, max(0, round((time.monotonic() - started) * 1000))

    def _run_item(
        self, check_id: str, title: str, kind: str, tool_id: str | None,
        callback: Callable[[], tuple[str, str]], total: int, mode: str,
    ) -> None:
        result, duration_ms = self._timed(callback)
        self._append_item(check_id, title, kind, tool_id, result, total, duration_ms, mode)

    def _append_item(
        self, check_id: str, title: str, kind: str, tool_id: str | None,
        result: tuple[str, str], total: int, duration_ms: int, depth: str,
    ) -> None:
        status, message = result
        with self._lock:
            item = {
                "id": check_id, "title": title, "kind": kind, "toolId": tool_id,
                "status": status, "message": message, "durationMs": duration_ms, "depth": depth,
            }
            self._snapshot["items"].append(item)
            self._snapshot["completed"] += 1
            self._snapshot["percent"] = round(self._snapshot["completed"] * 100 / max(total, 1))
            progress = {
                "completed": self._snapshot["completed"], "total": total,
                "percent": self._snapshot["percent"], "current": title,
            }
        level = "success" if status == "ready" else "warning" if status in ("warning", "unknown") else "error"
        self._emit_log(level, check_id, f"{message} ({duration_ms} ms)")
        self.event_sink("sdk_check.item", item)
        self.event_sink("sdk_check.progress", progress)

    def _config(self):
        return load_app_config(self.workspace.config_path())

    def _check_workspace(self) -> tuple[str, str]:
        return "ready", str(self.workspace.require_root())

    @staticmethod
    def _check_python() -> tuple[str, str]:
        return "ready", f"{platform.python_implementation()} {platform.python_version()}"

    @staticmethod
    def _check_command_version(command: list[str], name: str) -> tuple[str, str]:
        try:
            result = subprocess.run(
                command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, encoding="utf-8", errors="replace", timeout=3, check=False,
                **hidden_process_kwargs(),
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            return "error", f"{name}版本检查失败: {exc}"
        output = result.stdout.strip().splitlines()
        if result.returncode != 0 or not output:
            return "error", f"{name}版本检查失败，退出码{result.returncode}"
        return "ready", output[0].strip()

    def _check_python_deep(self) -> tuple[str, str]:
        runtime = "冻结运行时" if getattr(sys, "frozen", False) else "源码运行时"
        return "ready", f"{platform.python_implementation()} {platform.python_version()}，{runtime}"

    def _check_keil(self) -> tuple[str, str]:
        try:
            uv4 = find_uv4(None)
            with self._lock:
                self._resolved_uv4 = uv4
            return "ready", str(uv4)
        except FileNotFoundError as exc:
            return "error", str(exc)

    def _check_keil_deep(self) -> tuple[str, str]:
        status, message = self._check_keil()
        if status == "error":
            return status, message
        uv4 = Path(message)
        armcc = uv4.parent.parent / "ARM" / "ARMCC" / "Bin" / "armcc.exe"
        if not armcc.is_file():
            return "error", f"UV4存在，但未找到ARMCC: {armcc}"
        version = ""
        if os.name == "nt":
            powershell = shutil.which("powershell.exe") or shutil.which("powershell")
            if powershell:
                result = self._check_command_version([
                    powershell, "-NoProfile", "-NonInteractive", "-Command",
                    "(Get-Item -LiteralPath $args[0]).VersionInfo.FileVersion", str(uv4),
                ], "Keil")
                if result[0] == "ready":
                    version = result[1]
        suffix = f"，版本 {version}" if version else ""
        return "ready", f"{uv4}{suffix}，ARMCC可用"

    def _check_project(self) -> tuple[str, str]:
        config = self._config()
        path = config.keil_project
        return (
            ("ready", f"IOC={config.ioc_path}；target={config.target}；Keil={path}")
            if config.ioc_path.is_file() and path.is_file()
            else ("error", f"自动匹配工程不存在: IOC={config.ioc_path}；Keil={path}")
        )

    def _check_project_deep(self) -> tuple[str, str]:
        config = self._config()
        path = config.keil_project
        if not path.is_file():
            return "error", f"工程不存在: {path}"
        try:
            root = ET.parse(path).getroot()
        except (OSError, ET.ParseError) as exc:
            return "error", f"Keil工程XML无效: {exc}"
        target = next(
            (item for item in root.findall(".//Target") if item.findtext("TargetName") == config.target),
            None,
        )
        if target is None:
            return "error", f"Keil target不存在: {config.target}"
        output = (target.findtext(".//OutputDirectory") or "").strip()
        if not output:
            return "error", f"Keil target未配置输出目录: {config.target}"
        return "ready", (
            f"IOC={config.ioc_path}，Keil={path}，"
            f"target={config.target}，output={output}")

    def _check_dts(self) -> tuple[str, str]:
        try:
            _header, _source, stale = generate_for_app(
                self.workspace.config_path(),
                check=True,
                progress=lambda percent, stage, message: self._emit_log(
                    "debug", "dts", f"{percent}% {stage}: {message}"
                ),
            )
            return ("warning", "DTS生成文件已过期") if stale else ("ready", "DTS与生成C/H一致")
        except (OSError, ValueError) as exc:
            return "error", str(exc)

    def _check_synced(self) -> tuple[str, str]:
        config = self._config()
        detected = detect_synced_app(config.keil_project) if config.keil_project.is_file() else None
        if detected == config.app:
            return "ready", f"已同步 {detected}"
        return "warning", f"工具App={config.app}，Keil={detected or '未识别'}"

    @staticmethod
    def _ports() -> list[dict[str, str]]:
        try:
            from serial.tools import list_ports
            return [{"device": p.device, "description": p.description or "", "hwid": p.hwid or ""} for p in list_ports.comports()]
        except ImportError:
            return []

    def _check_serial(self) -> tuple[str, str]:
        ports = self._ports()
        return ("ready", "、".join(p["device"] for p in ports)) if ports else ("warning", "未检测到串口")

    def _check_probe(self) -> tuple[str, str]:
        with self._lock:
            if self._probe_result is not None:
                return self._probe_result
        config = self._config()
        default_probe = str(self.workspace.settings.get().get("default_probe", "dap"))
        candidates = [default_probe, *(probe for probe in ("dap", "stlink") if probe != default_probe)]
        results = [check_probe(probe, config.device, 1200) for probe in candidates]
        connected = next((result for result in results if result.connected), None)
        if connected is not None:
            value = "ready", f"{connected.message}（{connected.duration_ms}ms）"
        else:
            present = next((result for result in results if result.available), None)
            value = (
                ("error", f"{present.message}（{present.duration_ms}ms）")
                if present is not None else
                ("error", "；".join(result.message for result in results))
            )
        with self._lock:
            self._probe_result = value
        return value

    def _check_probe_environment(self) -> tuple[str, str]:
        status, message = self._check_probe()
        return ("warning", message) if status == "error" else (status, message)

    def _check_dtc(self) -> tuple[str, str]:
        dtc = shutil.which("dtc")
        if not dtc:
            return "warning", "未安装dtc；当前使用SDK纯Python DTS解析器"
        output_path = Path(tempfile.gettempdir()) / "ark_sdk_studio_check.dtb"
        try:
            result = subprocess.run(
                [dtc, "-I", "dts", "-O", "dtb", "-o", str(output_path),
                 str(self.workspace.config_path())],
                 cwd=self.workspace.require_root(), stdout=subprocess.PIPE,
                 stderr=subprocess.STDOUT, text=True, encoding="utf-8",
                 errors="replace", timeout=5, check=False,
                 **hidden_process_kwargs(),
            )
            if result.returncode != 0:
                return "error", result.stdout.strip() or f"dtc退出码{result.returncode}"
            return "ready", f"{dtc}：Linux DTS语法兼容检查通过"
        except (OSError, subprocess.TimeoutExpired) as exc:
            return "error", f"dtc检查失败: {exc}"
        finally:
            try:
                output_path.unlink(missing_ok=True)
            except OSError:
                pass

    def _check_tool(self, tool: ToolSpec, mode: str) -> tuple[str, str]:
        handler = str(tool.check.get("handler", ""))
        quick_checks = {
            "workspace": self._check_workspace,
            "app_config": lambda: ("ready", str(self.workspace.config_path())),
            "dts_config": lambda: ("ready", str(self.workspace.config_path())),
            "project": self._check_project,
            "keil": self._check_keil,
            "keil_synced": self._check_keil_synced,
            "keil_built": self._check_keil_built,
            "keil_synced_probe": self._check_keil_synced_probe,
            "keil_built_probe": self._check_keil_built_probe,
            "project_clone": self._check_project_clone,
            "project_create": self._check_project_create,
            "project_package": self._check_project_package,
            "memory_report": self._check_memory_report,
            "serial_monitor": self._check_serial_monitor,
            "dts_editor": self._check_dts_editor,
            "encoding_convert": self._check_encoding_convert,
            "always": lambda: ("ready", "可用"),
        }
        deep_checks = {
            **quick_checks,
            "project": self._check_project_deep,
            "keil": self._check_keil_deep,
            "keil_synced": self._check_keil_synced_deep,
            "keil_built": self._check_keil_built,
            "keil_synced_probe": self._check_keil_synced_probe,
            "keil_built_probe": self._check_keil_built_probe,
        }
        checks = deep_checks if mode == "deep" else quick_checks
        callback = checks.get(handler)
        if callback is None:
            return "error", f"未知检查处理器: {handler}"
        timeout = int(tool.check.get("timeout_ms", 5000)) / 1000
        results: queue.Queue[tuple[str, str]] = queue.Queue(maxsize=1)

        def invoke() -> None:
            try:
                results.put(callback())
            except Exception as exc:
                results.put(("error", f"{type(exc).__name__}: {exc}"))

        worker = threading.Thread(target=invoke, daemon=True)
        worker.start()
        try:
            return results.get(timeout=timeout)
        except queue.Empty:
            return "error", f"检查超时（{timeout:g}s）"

    def _check_keil_synced_deep(self) -> tuple[str, str]:
        status, message = self._check_keil_synced()
        if status == "error":
            return status, message
        deep_status, deep_message = self._check_keil_deep()
        return (deep_status, deep_message) if deep_status == "error" else ("ready", f"{message}；{deep_message}")

    def _check_keil_synced(self) -> tuple[str, str]:
        status, message = self._check_keil()
        if status == "error":
            return status, message
        config = self._config()
        if not config.keil_project.is_file():
            return "error", f"工程不存在: {config.keil_project}"
        detected = detect_synced_app(config.keil_project)
        if detected != config.app:
            return "error", f"请先同步Keil：当前 {detected or '未识别'}，目标 {config.app}"
        return "ready", message

    def _check_keil_built(self) -> tuple[str, str]:
        status, message = self._check_keil_synced()
        if status == "error":
            return status, message
        config = self._config()
        try:
            root = ET.parse(config.keil_project).getroot()
        except (OSError, ET.ParseError) as exc:
            return "error", f"Keil工程XML无效: {exc}"
        target = next((node for node in root.findall(".//Target") if node.findtext("TargetName") == config.target), None)
        if target is None:
            return "error", f"Keil target不存在: {config.target}"
        output = (target.findtext("./TargetOption/TargetCommonOption/OutputDirectory") or "").strip()
        output_name = (target.findtext("./TargetOption/TargetCommonOption/OutputName") or config.target).strip()
        axf = (config.keil_project.parent / output / f"{output_name}.axf").resolve()
        return ("ready", f"已有固件: {axf}") if axf.is_file() else ("error", f"请先编译生成固件: {axf}")

    def _check_keil_synced_probe(self) -> tuple[str, str]:
        status, message = self._check_keil_synced()
        if status == "error":
            return status, message
        probe_status, probe_message = self._check_probe()
        return (
            ("ready", f"{message}；{probe_message}")
            if probe_status == "ready" else ("error", probe_message)
        )

    def _check_keil_built_probe(self) -> tuple[str, str]:
        status, message = self._check_keil_built()
        if status == "error":
            return status, message
        probe_status, probe_message = self._check_probe()
        return (
            ("ready", f"{message}；{probe_message}")
            if probe_status == "ready" else ("error", probe_message)
        )

    def _check_project_clone(self) -> tuple[str, str]:
        config = self._config()
        return (
            ("ready", f"克隆器可用；当前模板 {config.cube_project_root}")
            if config.cube_project_root.is_dir()
            else ("error", f"CubeMX模板不存在: {config.cube_project_root}")
        )

    def _check_project_create(self) -> tuple[str, str]:
        config = self._config()
        if not config.cube_project_root.is_dir():
            return "error", f"当前CubeMX模板不存在: {config.cube_project_root}"
        return "ready", f"新建App工具可用；默认模板 {config.cube_project_root}"

    def _check_project_package(self) -> tuple[str, str]:
        status, message = self._check_keil_synced()
        if status == "error":
            return status, message
        return "ready", f"打包器可用；{message}"

    def _check_memory_report(self) -> tuple[str, str]:
        status, message = self._check_keil_built()
        if status == "error":
            return status, message
        config = self._config()
        maps = list(config.keil_project.parent.rglob("*.map"))
        return ("ready", f"可分析映射文件: {maps[0]}") if maps else (
            "error", "未找到Keil MAP文件，请先编译")

    def _check_serial_monitor(self) -> tuple[str, str]:
        try:
            import serial  # noqa: F401
        except ImportError:
            return "error", "缺少pyserial，请安装studio/requirements.txt"
        return self._check_serial()

    def _check_dts_editor(self) -> tuple[str, str]:
        config = self.workspace.config_path()
        editor = shutil.which("code") or shutil.which("code.cmd")
        if editor:
            return "ready", f"将使用VS Code打开 {config}"
        return ("ready", f"将使用系统关联程序打开 {config}") if os.name == "nt" else (
            "error", "未找到VS Code或系统文件打开器")

    def _check_encoding_convert(self) -> tuple[str, str]:
        return "ready", "支持UTF-8与GB2312代码/配置文件转换"
