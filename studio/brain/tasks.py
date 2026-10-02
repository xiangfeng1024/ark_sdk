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

"""Queued task execution with resource locks, real progress and structured logs."""

from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import importlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import threading
import time
from typing import Any, Callable
import uuid

from studio.tooling.flash_keil import configure_probe
from studio.tooling.build_cleanup import CleanupCancelled, create_cleanup_plan, execute_cleanup_plan
from studio.tooling.keil_tools import run_uv4
from studio.tooling.probe_check import check_probe
from studio.tooling.project_config import load_app_config, resolve_selection, sync_keil_project
from studio.tooling.ark_dts import generate_for_app
from studio.tooling.process_runner import hidden_process_kwargs

from .errors import BrainError
from .models import LOG_LEVELS, LogRecord, TaskRecord, ToolSpec, utc_now
from .registry import ToolRegistry
from .sdk_check import SDKCheckService
from .settings import HistoryStore, SettingsStore
from .validators import run_validators
from .workspace import WorkspaceService
from .resource_locks import ResourceLockManager
from .artifacts import ArtifactService


EventSink = Callable[[str, dict[str, Any]], None]
PROGRESS_PATTERN = re.compile(r"\[ARK_PROGRESS\]\s+(\d+)\s+(.+)")
EMBEDDED_HANDLERS = {
    "app_create": "studio.tooling.app_create",
    "dts_editor": "studio.tooling.dts_editor",
    "encoding_convert": "studio.tooling.encoding_convert",
    "memory_analyzer": "studio.tooling.memory_analyzer",
    "package_project": "studio.tooling.package_project",
    "project_clone": "studio.tooling.cube_project_tool",
    "serial_monitor": "studio.tooling.serial_monitor",
}


class _TaskOutput:
    def __init__(self, engine: "TaskEngine", record: TaskRecord) -> None:
        self.engine = engine
        self.record = record
        self.pending = ""

    def write(self, text: str) -> int:
        self.pending += text
        while "\n" in self.pending:
            line, self.pending = self.pending.split("\n", 1)
            if line:
                self.engine._consume_output(self.record, line + "\n")
        return len(text)

    def flush(self) -> None:
        if self.pending:
            self.engine._consume_output(self.record, self.pending + "\n")
            self.pending = ""


class TaskContext:
    def __init__(self, engine: "TaskEngine", record: TaskRecord) -> None:
        self.engine = engine
        self.record = record

    @property
    def cancelled(self) -> bool:
        return self.engine._cancel_events[self.record.id].is_set()

    def log(self, text: str, level: str = "info", source: str = "plugin") -> None:
        self.engine._log(self.record, text, level, source)

    def progress(self, percent: int, stage: str, message: str) -> None:
        self.engine._progress(self.record, percent, stage, message)


class TaskEngine:
    def __init__(
        self, workspace: WorkspaceService, registry: ToolRegistry, history: HistoryStore,
        settings: SettingsStore, sdk_check: SDKCheckService, event_sink: EventSink,
        resource_locks: ResourceLockManager | None = None,
        artifacts: ArtifactService | None = None,
    ) -> None:
        self.workspace = workspace
        self.registry = registry
        self.history = history
        self.settings = settings
        self.sdk_check = sdk_check
        self.event_sink = event_sink
        self._records: dict[str, TaskRecord] = {}
        self._history = history.load()
        self._cancel_events: dict[str, threading.Event] = {}
        self._processes: dict[str, subprocess.Popen[str]] = {}
        self._embedded_lock = threading.Lock()
        self.resource_locks = resource_locks or ResourceLockManager()
        self.artifacts = artifacts

    def list(self) -> list[dict[str, Any]]:
        current = sorted(self._records.values(), key=lambda item: item.created_at, reverse=True)
        known = {item.id for item in current}
        historical = [{**item, "logs": []} for item in reversed(self._history) if str(item.get("id")) not in known]
        return [item.to_dict() for item in current] + historical[:100]

    def get(self, task_id: str) -> dict[str, Any]:
        if task_id in self._records:
            return self._records[task_id].to_dict()
        for item in reversed(self._history):
            if item.get("id") == task_id:
                result = dict(item)
                result["logs"] = self.history.read_logs(item)
                return result
        raise BrainError("UNKNOWN_TASK", f"unknown task '{task_id}'")

    def clear_history(self) -> dict[str, int]:
        result = self.history.clear_completed()
        completed_ids = [task_id for task_id, record in self._records.items() if record.state not in ("queued", "running")]
        for task_id in completed_ids:
            self._records.pop(task_id, None)
            self._cancel_events.pop(task_id, None)
        result["removed"] += len(completed_ids)
        self._history = self.history.load()
        self.event_sink("task.history.cleared", result)
        return result

    def start(self, tool_id: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
        root = self.workspace.require_root()
        active = next((record for record in self._records.values() if record.state in ("queued", "running")), None)
        if active is not None:
            raise BrainError("TASK_BUSY", f"tool '{active.title}' is already running")
        tool = self.registry.get(tool_id)
        available, reason = self.sdk_check.tool_available(tool_id)
        if not available:
            raise BrainError("TOOL_UNAVAILABLE", reason)
        values = self.registry.validate_params(tool, params or {})
        if tool.parameter_mode != "dynamic":
            self.settings.set_tool_params(
                str(root), self.workspace.active_app, tool.id, values)
        record = TaskRecord(
            id=uuid.uuid4().hex, tool_id=tool.id, title=tool.title,
            app=self.workspace.active_app, params=values,
        )
        self._records[record.id] = record
        self._cancel_events[record.id] = threading.Event()
        self.event_sink("task.started", record.to_dict())
        threading.Thread(target=self._run, args=(tool, record), daemon=True).start()
        return record.to_dict()

    def cancel(self, task_id: str) -> dict[str, Any]:
        record = self._records.get(task_id)
        if record is None:
            raise BrainError("UNKNOWN_TASK", f"unknown task '{task_id}'")
        tool = self.registry.get(record.tool_id)
        if not tool.cancellable:
            raise BrainError("NOT_CANCELLABLE", f"task '{task_id}' cannot be cancelled")
        self._cancel_events[task_id].set()
        process = self._processes.get(task_id)
        if process is not None and process.poll() is None:
            self._terminate_process(process)
        self.resource_locks.notify()
        return record.to_dict()

    def _resources(self, tool: ToolSpec, record: TaskRecord) -> tuple[str, ...]:
        config = load_app_config(self.workspace.config_path(record.app))
        values = {
            "project": str(config.keil_project.resolve()).lower(),
            "probe": str(record.params.get("probe", self.settings.get().get("default_probe", "dap"))),
        }
        values.update({key: str(value) for key, value in record.params.items()})
        resolved: list[str] = []
        for resource in tool.resources:
            value = resource
            for key, replacement in values.items():
                value = value.replace(f"${{{key}}}", replacement)
            resolved.append(value.lower())
        return tuple(resolved)

    def _acquire(self, record: TaskRecord, resources: tuple[str, ...]) -> bool:
        record.stage = "queued"
        record.message = "等待工程资源"
        return self.resource_locks.acquire(
            f"task:{record.id}", resources, self._cancel_events[record.id])

    def _release(self, record: TaskRecord, resources: tuple[str, ...]) -> None:
        self.resource_locks.release(f"task:{record.id}", resources)

    def _run(self, tool: ToolSpec, record: TaskRecord) -> None:
        resources = self._resources(tool, record)
        if not self._acquire(record, resources):
            self._finish(record, "cancelled", None, "任务在队列中被取消")
            return
        query_stop = threading.Event()
        query_thread: threading.Thread | None = None
        try:
            record.state = "running"
            record.started_at = utc_now()
            record.stage = "execute"
            record.message = "正在执行"
            self._emit_progress(record)
            if tool.progress.get("mode") == "query":
                query_thread = threading.Thread(target=self._query_loop, args=(tool, record, query_stop), daemon=True)
                query_thread.start()
            exit_code = self._execute(tool, record)
            if self._cancel_events[record.id].is_set():
                self._finish(record, "cancelled", exit_code, "任务已取消")
                return
            record.exit_code = exit_code
            if exit_code != 0:
                self._log(record, f"进程退出码 {exit_code}", "error", "brain")
                self._finish(record, "failed", exit_code, f"进程退出码 {exit_code}")
                return
            record.stage = "validate"
            record.message = "校验任务结果"
            self._emit_progress(record)
            output = "".join(entry.text for entry in record.logs)
            record.validations = run_validators(tool.validators, self.workspace.config_path(record.app), output)
            failed = [item for item in record.validations if not item["passed"]]
            if failed:
                self._log(record, failed[0]["message"], "error", "validator")
                self._finish(record, "failed", exit_code, failed[0]["message"])
                return
            self._finish(record, "succeeded", exit_code, None)
        except BrainError as exc:
            self._log(record, str(exc), "error", "brain")
            self._finish(record, "blocked", None, str(exc))
        except Exception as exc:
            message = f"{type(exc).__name__}: {exc}"
            self._log(record, message, "error", "brain")
            self._finish(record, "failed", None, message)
        finally:
            query_stop.set()
            if query_thread is not None:
                query_thread.join(timeout=1)
            self._processes.pop(record.id, None)
            self._release(record, resources)

    def _execute(self, tool: ToolSpec, record: TaskRecord) -> int:
        kind = tool.entrypoint.get("kind")
        if kind == "builtin":
            return self._execute_builtin(str(tool.entrypoint.get("handler", "")), record)
        raise BrainError("INVALID_ENTRYPOINT", f"unsupported entrypoint for {tool.id}")

    def _execute_builtin(self, handler: str, record: TaskRecord) -> int:
        params = record.params
        if handler in ("dts_check", "dts_generate"):
            check = handler == "dts_check"
            context = TaskContext(self, record)
            context.log(
                f"ark_dts {'check' if check else 'generate'} {self.workspace.config_path(record.app)}",
                "command",
                "dts",
            )
            header, source, status = generate_for_app(
                self.workspace.config_path(record.app),
                check=check,
                progress=lambda percent, stage, message: context.progress(percent, stage, message),
            )
            context.log(
                f"DTS generated files: {'stale' if status and check else 'generated' if status else 'valid and current'}",
                "warning" if status and check else "success",
                "dts",
            )
            context.log(f"header: {header}", "info", "dts")
            context.log(f"source: {source}", "info", "dts")
            return 1 if check and status else 0
        if handler == "sync":
            context = TaskContext(self, record)
            config_path = self.workspace.config_path(record.app)
            context.progress(5, "prepare", "同步任务已准备")
            context.log(f"同步App配置到Keil: {config_path}", "command", "sync")

            def config_progress(percent: int, stage: str, message: str) -> None:
                mapped = 5 + round(min(max(percent, 0), 92) * 35 / 92)
                context.progress(mapped, f"config:{stage}", message)

            header, source, changed = generate_for_app(config_path, progress=config_progress)
            context.log(f"DTS C/H: {'generated' if changed else 'already current'}", "info", "sync")
            context.log(f"header: {header}", "debug", "sync")
            context.log(f"source: {source}", "debug", "sync")
            if context.cancelled:
                return 130

            context.progress(48, "resolve", "正在解析App、HAL和组件依赖")
            config = load_app_config(config_path)
            selection = resolve_selection(config)
            source_count = sum(len(group) for group in selection.groups.values())
            context.log(f"已解析 {source_count} 个SDK源文件", "info", "sync")
            if context.cancelled:
                return 130

            context.progress(65, "sync", "正在更新Keil工程分组和设备配置")
            project_changed = sync_keil_project(
                config.keil_project, selection, config.sdk_link,
                config.device, config.reset_and_run,
            )
            context.log(
                f"{'updated' if project_changed else 'already synchronized'}: {config.keil_project}",
                "success", "sync",
            )
            context.progress(92, "sync", "Keil工程同步完成，等待结果校验")
            return 0
        elif handler in ("build_incremental", "build_full"):
            context = TaskContext(self, record)
            context.progress(2, "prepare", "构建命令已准备")
            context.log("正在启动Keil，等待构建日志", "info", "keil")
            config_path = self.workspace.config_path(record.app)
            generate_for_app(config_path)
            config = load_app_config(config_path)
            return self._run_keil(
                record, self.sdk_check.require_uv4(), "-b" if handler == "build_incremental" else "-r",
                config.keil_project, config.target, config.keil_project.parent / ".keil" / "build.log",
                (2, 96),
            )
        elif handler == "flash_incremental":
            context = TaskContext(self, record)
            context.progress(2, "prepare", "烧录任务已准备")
            context.log("正在启动Keil，先执行增量编译", "info", "keil")
            probe = str(params.get("probe", self.settings.get().get("default_probe", "dap")))
            config_path = self.workspace.config_path(record.app)
            generate_for_app(config_path)
            config = load_app_config(config_path)
            probe_result = check_probe(probe, config.device, 1500)
            context.log(probe_result.message, "success" if probe_result.connected else "error", "probe")
            if not probe_result.connected:
                raise BrainError("PROBE_NOT_CONNECTED", probe_result.message)
            configure_probe(config.keil_project, probe)
            sync_keil_project(
                config.keil_project, resolve_selection(config), config.sdk_link,
                config.device, config.reset_and_run,
            )
            executable = self.sdk_check.require_uv4()
            result = self._run_keil(
                record, executable, "-b", config.keil_project, config.target,
                config.keil_project.parent / ".keil" / "build.log", (2, 55),
            )
            if result != 0:
                return result
            if self._cancel_events[record.id].is_set():
                return 130
            return self._run_keil(
                record, executable, "-f", config.keil_project, config.target,
                config.keil_project.parent / ".keil" / "flash.log", (55, 96),
                probe,
            )
        elif handler == "flash_only":
            context = TaskContext(self, record)
            context.progress(2, "prepare", "烧录任务已准备")
            probe = str(params.get("probe", self.settings.get().get("default_probe", "dap")))
            config = load_app_config(self.workspace.config_path(record.app))
            probe_result = check_probe(probe, config.device, 1500)
            context.log(probe_result.message, "success" if probe_result.connected else "error", "probe")
            if not probe_result.connected:
                raise BrainError("PROBE_NOT_CONNECTED", probe_result.message)
            configure_probe(config.keil_project, probe)
            context.progress(5, "prepare", "探针配置已更新")
            return self._run_keil(
                record, self.sdk_check.require_uv4(), "-f", config.keil_project, config.target,
                config.keil_project.parent / ".keil" / "flash.log", (5, 96),
                probe,
            )
        elif handler == "clean":
            context = TaskContext(self, record)
            try:
                plan = create_cleanup_plan(
                    self.workspace.config_path(record.app), context.progress,
                    lambda: context.cancelled,
                )
                context.log(
                    f"待清理 {len(plan.files)} 个文件，{plan.total_bytes} 字节",
                    "info", "clean",
                )
                execute_cleanup_plan(plan, context.progress, lambda: context.cancelled)
                context.log("构建产物清理完成", "success", "clean")
                return 0
            except CleanupCancelled:
                context.log("用户取消清理，已停止后续删除", "warning", "clean")
                return 130
        elif handler == "demo_progress":
            context = TaskContext(self, record)
            for percent in range(5, 101, 5):
                if context.cancelled:
                    return 130
                context.progress(percent, "demo", f"示例进度 {percent}%")
                for part in range(6):
                    context.log(f"demo step {percent}.{part}", "debug", "demo")
                time.sleep(0.025)
            return 0
        elif handler in EMBEDDED_HANDLERS:
            return self._execute_embedded(handler, record)
        else:
            raise BrainError("UNKNOWN_HANDLER", f"unknown builtin handler '{handler}'")

    def _run_keil(
        self, record: TaskRecord, uv4: Path, operation: str, project: Path,
        target: str | None, log_file: Path, progress_range: tuple[int, int],
        flash_probe: str = "dap",
    ) -> int:
        return run_uv4(
            uv4, operation, project, target, log_file,
            on_output=lambda text: self._consume_output(record, text),
            progress_range=progress_range,
            cancelled=lambda: self._cancel_events[record.id].is_set(),
            on_process=lambda process: self._processes.__setitem__(record.id, process),
            flash_probe=flash_probe,
        )

    def _execute_embedded(self, handler: str, record: TaskRecord) -> int:
        tool = self.registry.get(record.tool_id)
        args = tool.entrypoint.get("args", [])
        if not isinstance(args, list) or any(not isinstance(item, str) for item in args):
            raise BrainError("INVALID_ENTRYPOINT", f"invalid embedded entrypoint for {tool.id}")
        root = self.workspace.require_root()
        replacements = {f"${{param.{key}}}": str(value) for key, value in record.params.items()}
        config = load_app_config(self.workspace.config_path(record.app))
        replacements.update({
            "${app_config}": str(config.path),
            "${workspace}": str(root),
            "${workspace_parent}": str(root.parent),
            "${cube_project}": str(config.cube_project_root),
        })
        if any("${uv4}" in item for item in args):
            replacements["${uv4}"] = str(self.sdk_check.require_uv4())
        resolved: list[str] = []
        for original in args:
            value = original
            for token, replacement in replacements.items():
                value = value.replace(token, replacement)
            if "${" in value:
                raise BrainError("INVALID_PARAMS", f"unresolved entrypoint token: {value}")
            resolved.append(value)
        module = importlib.import_module(EMBEDDED_HANDLERS[handler])
        callback = getattr(module, "main", None)
        if not callable(callback):
            raise BrainError("INVALID_ENTRYPOINT", f"missing main() for embedded handler '{handler}'")
        output = _TaskOutput(self, record)
        old_argv = sys.argv
        with self._embedded_lock:
            sys.argv = [handler, *resolved]
            try:
                with redirect_stdout(output), redirect_stderr(output):
                    try:
                        return int(callback() or 0)
                    except SystemExit as exc:
                        return int(exc.code or 0) if isinstance(exc.code, (int, type(None))) else 1
            finally:
                output.flush()
                sys.argv = old_argv

    def _spawn(self, record: TaskRecord, command: list[str], cwd: Path) -> int:
        self._log(record, subprocess.list2cmdline(command), "command", "process")
        environment = os.environ.copy()
        environment["PYTHONIOENCODING"] = "utf-8"
        flags = (
            subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.CREATE_NO_WINDOW
            if os.name == "nt" else 0
        )
        process = subprocess.Popen(
            command, cwd=cwd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, encoding="utf-8",
            errors="replace", bufsize=1, env=environment,
            creationflags=flags, close_fds=True,
        )
        self._processes[record.id] = process
        assert process.stdout is not None
        try:
            for line in process.stdout:
                self._consume_output(record, line)
                if self._cancel_events[record.id].is_set() and process.poll() is None:
                    self._terminate_process(process)
        finally:
            process.stdout.close()
        return process.wait()

    def _consume_output(self, record: TaskRecord, text: str) -> None:
        normalized = text.replace("\r\n", "\n").replace("\r", "\n")
        for line in normalized.splitlines(keepends=True):
            match = PROGRESS_PATTERN.search(line.strip())
            if match:
                percent = int(match.group(1))
                if record.tool_id == "keil.flash.incremental":
                    stage = "flash" if percent >= 55 else "build"
                elif record.tool_id.startswith("keil.flash"):
                    stage = "flash"
                elif record.tool_id.startswith("keil.build"):
                    stage = "build"
                else:
                    stage = {
                        "project.clone": "clone",
                        "project.create_app": "create",
                        "project.components": "dts",
                        "project.package": "package",
                        "analysis.memory": "analyze",
                        "debug.serial": "serial",
                    }.get(record.tool_id, "execute")
                self._progress(record, percent, stage, match.group(2))
            elif line:
                self._log(record, line, "info", "process")

    def _query_loop(self, tool: ToolSpec, record: TaskRecord, stop: threading.Event) -> None:
        interval = int(tool.progress.get("interval_ms", 500)) / 1000
        while not stop.wait(interval):
            value = self._query_progress(tool, record)
            if value is not None:
                self._progress(record, value[0], value[1], value[2], origin="query")

    def _query_progress(self, tool: ToolSpec, record: TaskRecord) -> tuple[int, str, str] | None:
        if tool.progress.get("handler") != "json_file":
            return None
        raw_path = str(tool.progress.get("path", ""))
        raw_path = raw_path.replace("${workspace}", str(self.workspace.require_root())).replace("${task_id}", record.id)
        path = Path(raw_path)
        if not path.is_file():
            return None
        try:
            value = json.loads(path.read_text(encoding="utf-8"))
            return int(value["percent"]), str(value.get("stage", "execute")), str(value.get("message", "正在执行"))
        except (OSError, ValueError, KeyError, TypeError):
            return None

    @staticmethod
    def _terminate_process(process: subprocess.Popen[str]) -> None:
        if process.poll() is not None:
            return
        if os.name == "nt":
            subprocess.run(
                ["taskkill", "/PID", str(process.pid), "/T", "/F"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                check=False,
                **hidden_process_kwargs(),
            )
        else:
            process.terminate()

    def _log(self, record: TaskRecord, text: str, level: str = "info", source: str = "brain") -> None:
        if level not in LOG_LEVELS:
            level = "info"
        timestamp = utc_now() if bool(self.settings.get().get("log_timestamps", True)) else ""
        entry = LogRecord(len(record.logs) + 1, timestamp, level, source, text if text.endswith("\n") else text + "\n")
        record.logs.append(entry)
        if len(record.logs) > 5000:
            del record.logs[:1000]
        self.event_sink("task.log", {"taskId": record.id, "entry": entry.to_dict()})

    def _emit_progress(self, record: TaskRecord) -> None:
        self.event_sink("task.progress", {
            "taskId": record.id, "state": record.state, "percent": record.percent,
            "stage": record.stage, "message": record.message,
        })

    def _progress(self, record: TaskRecord, percent: int, stage: str, message: str, origin: str = "event") -> None:
        mode = str(self.registry.get(record.tool_id).progress.get("mode", "completion"))
        if mode == "completion" or (mode == "query" and origin != "query") or (mode == "event" and origin == "query"):
            return
        record.percent = max(record.percent, min(max(percent, 0), 100))
        record.stage = stage
        record.message = message
        self._emit_progress(record)

    def _finish(self, record: TaskRecord, state: str, exit_code: int | None, error: str | None) -> None:
        record.state = state
        record.exit_code = exit_code
        record.error = error
        record.finished_at = utc_now()
        if state == "succeeded":
            record.percent = 100
            record.stage = "complete"
            record.message = "任务完成"
            self._log(record, "任务完成", "success", "brain")
        elif state == "cancelled":
            record.stage = "cancelled"
            record.message = error or "任务已取消"
        elif state == "blocked":
            record.stage = "blocked"
            record.message = error or "执行条件不满足"
        else:
            record.stage = "failed"
            record.message = error or "任务失败"
        if self.artifacts is not None:
            try:
                record.artifacts = self.artifacts.list(record.app)
            except (OSError, ValueError):
                record.artifacts = []
        snapshot = record.to_dict()
        self.history.append(snapshot, [entry.to_dict() for entry in record.logs])
        self._history = self.history.load()
        self.event_sink("task.finished", snapshot)
