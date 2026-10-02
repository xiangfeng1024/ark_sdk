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

from __future__ import annotations

import json
import os
from pathlib import Path
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch


SCRIPT_ROOT = Path(__file__).resolve().parents[1]
SDK_ROOT = SCRIPT_ROOT.parent
sys.path.insert(0, str(SCRIPT_ROOT))

from studio.brain.registry import ToolRegistry
from studio.brain.errors import BrainError
from studio.brain.models import TaskRecord
from studio.brain.sdk_check import SDKCheckService
from studio.brain.settings import HistoryStore, SettingsStore
from studio.brain.tasks import TaskEngine
from studio.brain.workspace import WorkspaceService


def wait_check(checker: SDKCheckService) -> None:
    with patch.object(checker, "_check_probe", return_value=("ready", "DAP已连接目标板")):
        checker.start(force=True)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if checker.snapshot()["state"] in ("succeeded", "failed"):
                return
            time.sleep(0.02)
    raise AssertionError("SDK check timed out")


class TaskTests(unittest.TestCase):
    def test_sync_runs_in_process_and_reports_real_stages(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            settings = SettingsStore(Path(temporary))
            workspace = WorkspaceService(settings, SDK_ROOT)
            registry = ToolRegistry(SCRIPT_ROOT / "resources" / "tools")
            events: list[tuple[str, dict]] = []
            checker = SDKCheckService(workspace, registry, lambda _name, _data: None)
            engine = TaskEngine(workspace, registry, HistoryStore(settings), settings, checker, lambda name, data: events.append((name, data)))
            record = TaskRecord("sync", "keil.sync", "同步Keil", workspace.active_app, {})
            record.state = "running"
            engine._cancel_events[record.id] = threading.Event()

            def fake_generate(config_path, progress=None):
                if progress:
                    progress(15, "load", "配置已读取")
                    progress(92, "write", "生成文件已写入")
                return Path(config_path).parent / "include" / "ark_dts_generated.h", Path(config_path).parent / "src" / "ark_dts_generated.c", False

            with patch("studio.brain.tasks.generate_for_app", side_effect=fake_generate), patch("studio.brain.tasks.sync_keil_project", return_value=True):
                self.assertEqual(engine._execute_builtin("sync", record), 0)
            progress = [data for name, data in events if name == "task.progress"]
            self.assertTrue(any(item["stage"] == "resolve" and item["percent"] == 48 for item in progress))
            self.assertTrue(any(item["stage"] == "sync" and item["percent"] == 92 for item in progress))
            self.assertTrue(any("updated" in entry.text for entry in record.logs))

    def test_disabled_timestamps_skip_brain_log_time_generation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            settings = SettingsStore(Path(temporary))
            settings.update({"log_timestamps": False})
            workspace = WorkspaceService(settings, SDK_ROOT)
            registry = ToolRegistry(SCRIPT_ROOT / "resources" / "tools")
            checker = SDKCheckService(workspace, registry, lambda _name, _data: None)
            engine = TaskEngine(workspace, registry, HistoryStore(settings), settings, checker, lambda _name, _data: None)
            record = TaskRecord("log", "dts.check", "DTS", workspace.active_app, {})
            engine._log(record, "hello")
            self.assertEqual(record.logs[0].timestamp, "")

    def test_keil_build_reports_prepare_before_process_output(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            settings = SettingsStore(Path(temporary))
            workspace = WorkspaceService(settings, SDK_ROOT)
            registry = ToolRegistry(SCRIPT_ROOT / "resources" / "tools")
            events: list[tuple[str, dict]] = []
            checker = SDKCheckService(workspace, registry, lambda _name, _data: None)
            engine = TaskEngine(workspace, registry, HistoryStore(settings), settings, checker, lambda name, data: events.append((name, data)))
            record = TaskRecord("build", "keil.build.incremental", "增量编译", workspace.active_app, {})
            record.state = "running"
            engine._cancel_events[record.id] = threading.Event()
            with patch.object(checker, "require_uv4", return_value=Path("UV4.exe")), patch.object(engine, "_run_keil", return_value=0):
                self.assertEqual(engine._execute_builtin("build_incremental", record), 0)
            progress = [data for name, data in events if name == "task.progress"]
            self.assertTrue(progress)
            self.assertEqual(progress[0]["percent"], 2)
            self.assertEqual(progress[0]["stage"], "prepare")
            self.assertTrue(any("等待构建日志" in entry.text for entry in record.logs))

    def test_dts_task_reports_real_events_before_success_100(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            settings = SettingsStore(Path(temporary))
            workspace = WorkspaceService(settings, SDK_ROOT)
            workspace.open(app="c8t6_microcar_soil")
            registry = ToolRegistry(SCRIPT_ROOT / "resources" / "tools")
            events: list[tuple[str, dict]] = []
            checker = SDKCheckService(workspace, registry, lambda name, data: events.append((name, data)))
            wait_check(checker)
            events.clear()
            engine = TaskEngine(workspace, registry, HistoryStore(settings), settings, checker, lambda name, data: events.append((name, data)))
            task = engine.start("dts.check")
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline and not any(name == "task.finished" for name, _data in events):
                time.sleep(0.02)
            progress = [data["percent"] for name, data in events if name == "task.progress"]
            self.assertTrue(all(value in progress for value in (15, 35, 60, 82, 92)))
            self.assertNotIn(100, progress)
            finished = next(data for name, data in events if name == "task.finished")
            self.assertEqual(finished["id"], task["id"])
            self.assertEqual(finished["percent"], 100)
            self.assertEqual(finished["state"], "succeeded")

    def test_query_progress_reads_real_json_value(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            progress_file = root / "progress.json"
            tool_dir = root / "tools"
            tool_dir.mkdir()
            progress_file.write_text(json.dumps({"percent": 47, "stage": "copy", "message": "47/100 files"}), encoding="utf-8")
            manifest = {
                "schema_version": 1, "id": "test.query", "title": "Query", "description": "", "category": "test",
                "icon": "Wrench", "availability": "active", "parameter_mode": "none", "entrypoint": {"kind": "builtin", "handler": "demo_progress"},
                "check": {"handler": "always", "timeout_ms": 1000},
                "progress": {"mode": "query", "handler": "json_file", "path": str(progress_file), "interval_ms": 100},
                "inputs": [], "validators": [], "resources": [], "danger": "safe",
            }
            (tool_dir / "tool.json").write_text(json.dumps(manifest), encoding="utf-8")
            settings = SettingsStore(root / "state")
            workspace = WorkspaceService(settings, SDK_ROOT)
            registry = ToolRegistry(tool_dir)
            checker = SDKCheckService(workspace, registry, lambda _name, _data: None)
            engine = TaskEngine(workspace, registry, HistoryStore(settings), settings, checker, lambda _name, _data: None)
            value = engine._query_progress(registry.get("test.query"), TaskRecord("1", "test.query", "Query", workspace.active_app, {}))
            self.assertEqual(value, (47, "copy", "47/100 files"))

    def test_demo_task_streams_structured_progress_and_finishes(self) -> None:
        before = os.environ.get("ARK_STUDIO_TEST_MODE")
        os.environ["ARK_STUDIO_TEST_MODE"] = "1"
        try:
            with tempfile.TemporaryDirectory() as temporary:
                settings = SettingsStore(Path(temporary))
                workspace = WorkspaceService(settings, SDK_ROOT)
                registry = ToolRegistry(SCRIPT_ROOT / "resources" / "tools")
                events: list[tuple[str, dict]] = []
                checker = SDKCheckService(workspace, registry, lambda name, data: events.append((name, data)))
                wait_check(checker)
                engine = TaskEngine(workspace, registry, HistoryStore(settings), settings, checker, lambda name, data: events.append((name, data)))
                task = engine.start("test.demo_progress")
                deadline = time.monotonic() + 3
                while time.monotonic() < deadline and not any(name == "task.finished" for name, _ in events):
                    time.sleep(0.02)
                value = engine.get(task["id"])
                self.assertEqual(value["state"], "succeeded")
                self.assertEqual(value["percent"], 100)
                self.assertEqual(value["logs"][0]["level"], "debug")
                self.assertTrue(any(name == "task.progress" for name, _ in events))
        finally:
            if before is None:
                os.environ.pop("ARK_STUDIO_TEST_MODE", None)
            else:
                os.environ["ARK_STUDIO_TEST_MODE"] = before

    def test_completion_progress_holds_until_success(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            state = Path(temporary) / "state"
            tool_dir = Path(temporary) / "tools"
            tool_dir.mkdir()
            manifest = {
                "schema_version": 1, "id": "test.complete", "title": "Complete", "description": "", "category": "test",
                "icon": "Wrench", "availability": "active", "parameter_mode": "none", "entrypoint": {"kind": "builtin", "handler": "demo_progress"},
                "check": {"handler": "always", "timeout_ms": 1000}, "progress": {"mode": "completion"},
                "inputs": [], "validators": [], "resources": [], "danger": "safe",
            }
            (tool_dir / "tool.json").write_text(json.dumps(manifest), encoding="utf-8")
            settings = SettingsStore(state)
            workspace = WorkspaceService(settings, SDK_ROOT)
            registry = ToolRegistry(tool_dir)
            checker = SDKCheckService(workspace, registry, lambda _name, _data: None)
            wait_check(checker)
            engine = TaskEngine(workspace, registry, HistoryStore(settings), settings, checker, lambda _name, _data: None)
            # The builtin reports events, but the completion contract deliberately ignores them.
            task = engine.start("test.complete")
            time.sleep(0.01)
            self.assertEqual(engine.get(task["id"])["percent"], 0)

    def test_second_tool_is_rejected_while_a_task_is_active(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            tool_dir = root / "tools"
            tool_dir.mkdir()
            manifest = {
                "schema_version": 1, "title": "Lock", "description": "", "category": "test", "icon": "Wrench",
                "availability": "active", "parameter_mode": "none", "entrypoint": {"kind": "builtin", "handler": "demo_progress"},
                "check": {"handler": "always", "timeout_ms": 1000}, "progress": {"mode": "event"},
                "inputs": [], "validators": [], "resources": ["shared"], "danger": "safe",
            }
            for index in range(2):
                (tool_dir / f"tool{index}.json").write_text(json.dumps(dict(manifest, id=f"test.lock{index}", order=index)), encoding="utf-8")
            settings = SettingsStore(root / "state")
            workspace = WorkspaceService(settings, SDK_ROOT)
            registry = ToolRegistry(tool_dir)
            checker = SDKCheckService(workspace, registry, lambda _name, _data: None)
            wait_check(checker)
            engine = TaskEngine(workspace, registry, HistoryStore(settings), settings, checker, lambda _name, _data: None)
            first = engine.start("test.lock0")
            with self.assertRaisesRegex(BrainError, "already running") as raised:
                engine.start("test.lock1")
            self.assertEqual(raised.exception.code, "TASK_BUSY")
            self.assertIn(engine.get(first["id"])["state"], {"queued", "running", "succeeded"})


if __name__ == "__main__":
    unittest.main()
