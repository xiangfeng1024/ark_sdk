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

from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

SCRIPT_ROOT = Path(__file__).resolve().parents[1]
SDK_ROOT = SCRIPT_ROOT.parent
sys.path.insert(0, str(SCRIPT_ROOT))

from studio.brain.registry import ToolRegistry
from studio.brain.sdk_check import SDKCheckService
from studio.brain.settings import SettingsStore
from studio.brain.workspace import WorkspaceService


class SDKCheckTests(unittest.TestCase):
    def test_checks_every_tool_and_enables_connected_tools(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            workspace = WorkspaceService(SettingsStore(Path(temporary)), SDK_ROOT)
            registry = ToolRegistry(SCRIPT_ROOT / "resources" / "tools")
            events: list[str] = []
            checker = SDKCheckService(workspace, registry, lambda name, _data: events.append(name))
            with patch.object(checker, "_check_probe", return_value=("ready", "DAP已连接目标板")):
                checker.start(force=True)
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline and checker.snapshot()["state"] not in ("succeeded", "failed"):
                    time.sleep(0.02)
            snapshot = checker.snapshot()
            self.assertEqual(snapshot["percent"], 100)
            self.assertEqual(snapshot["mode"], "quick")
            self.assertGreaterEqual(snapshot["durationMs"], 0)
            self.assertTrue(all(item["depth"] == "quick" for item in snapshot["items"]))
            self.assertTrue(all(item["durationMs"] >= 0 for item in snapshot["items"]))
            self.assertEqual(len(snapshot["tools"]), len(registry.list()))
            self.assertTrue(snapshot["tools"]["project.clone"]["available"])
            self.assertTrue(snapshot["tools"]["project.create_app"]["available"])
            self.assertFalse(snapshot["tools"]["keil.flash.only"]["available"])
            self.assertIn("sdk_check.progress", events)

    def test_deep_check_runs_read_only_versions_and_records_duration(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            workspace = WorkspaceService(SettingsStore(Path(temporary)), SDK_ROOT)
            registry = ToolRegistry(SCRIPT_ROOT / "resources" / "tools")
            checker = SDKCheckService(workspace, registry, lambda _name, _data: None)
            with patch.object(checker, "_check_probe", return_value=("ready", "DAP已连接目标板")):
                checker.start(force=True, mode="deep")
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline and checker.snapshot()["state"] not in ("succeeded", "failed"):
                    time.sleep(0.02)
            snapshot = checker.snapshot()
            self.assertEqual(snapshot["state"], "succeeded")
            self.assertEqual(snapshot["mode"], "deep")
            self.assertGreaterEqual(snapshot["durationMs"], 0)
            self.assertTrue(all(item["depth"] == "deep" for item in snapshot["items"]))
            self.assertIn("Python", next(item["message"] for item in snapshot["items"] if item["id"] == "python"))

    def test_frozen_python_deep_check_does_not_execute_studio_exe(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            workspace = WorkspaceService(SettingsStore(Path(temporary)), SDK_ROOT)
            registry = ToolRegistry(SCRIPT_ROOT / "resources" / "tools")
            checker = SDKCheckService(workspace, registry, lambda _name, _data: None)
            with patch.object(sys, "frozen", True, create=True), \
                 patch("subprocess.run", side_effect=AssertionError("must not execute frozen Studio")):
                status, message = checker._check_python_deep()
            self.assertEqual(status, "ready")
            self.assertIn("冻结运行时", message)

    def test_disconnected_probe_is_environment_warning_but_tool_error(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            workspace = WorkspaceService(SettingsStore(Path(temporary)), SDK_ROOT)
            registry = ToolRegistry(SCRIPT_ROOT / "resources" / "tools")
            checker = SDKCheckService(workspace, registry, lambda _name, _data: None)
            with patch.object(checker, "_check_probe", return_value=("error", "探针未连接")):
                self.assertEqual(checker._check_probe_environment(), ("warning", "探针未连接"))

    def test_dts_check_runs_in_process(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            workspace = WorkspaceService(SettingsStore(Path(temporary)), SDK_ROOT)
            registry = ToolRegistry(SCRIPT_ROOT / "resources" / "tools")
            checker = SDKCheckService(workspace, registry, lambda _name, _data: None)
            with patch("subprocess.run", side_effect=AssertionError("must not spawn")):
                status, message = checker._check_dts()
            self.assertEqual(status, "ready")
            self.assertIn("一致", message)


if __name__ == "__main__":
    unittest.main()
