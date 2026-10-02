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

import os
from pathlib import Path
import sys
import unittest
import json
import tempfile


SCRIPT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_ROOT))

from studio.brain.registry import ToolRegistry
from studio.brain.errors import BrainError


class RegistryTests(unittest.TestCase):
    def test_discovers_only_executable_tools(self) -> None:
        registry = ToolRegistry(SCRIPT_ROOT / "resources" / "tools")
        tools = {tool["id"]: tool for tool in registry.list()}
        self.assertIn("keil.sync", tools)
        self.assertEqual(tools["keil.sync"]["availability"], "active")
        self.assertEqual(tools["project.clone"]["availability"], "active")
        self.assertEqual(tools["project.create_app"]["availability"], "active")
        self.assertTrue(all(tool["availability"] == "active" for tool in tools.values()))
        self.assertTrue(all(tool["check"]["handler"] != "planned" for tool in tools.values()))
        self.assertTrue(all(tool["progress"]["mode"] in {"event", "query"} for tool in tools.values()))
        self.assertEqual(tools["keil.build.full"]["parameterMode"], "none")
        self.assertEqual(tools["keil.flash.only"]["parameterMode"], "fixed")
        self.assertEqual(tools["project.create_app"]["parameterMode"], "dynamic")
        self.assertEqual(tools["debug.serial"]["parameterMode"], "dynamic")
        self.assertEqual(tools["sdk.encoding.convert"]["parameterMode"], "dynamic")
        encoding_inputs = {item["name"]: item for item in tools["sdk.encoding.convert"]["inputs"]}
        self.assertEqual(encoding_inputs["folder"]["type"], "directory")
        self.assertEqual(encoding_inputs["target"]["options"], ["UTF-8", "GB2312"])
        self.assertTrue(all(item["remember"] for item in tools["debug.serial"]["inputs"]))
        self.assertFalse(tools["debug.serial"]["cancellable"])
        self.assertEqual(tools["debug.serial"]["uiMode"], "serial_terminal")
        self.assertTrue(all(tool["entrypoint"]["kind"] == "builtin" for tool in tools.values()))
        self.assertTrue(all(not ({"script", "file", "module"} & set(tool["entrypoint"])) for tool in tools.values()))
        self.assertNotIn("test.demo_progress", tools)
        self.assertEqual(len(tools), 15)

    def test_test_only_tool_is_opt_in(self) -> None:
        before = os.environ.get("ARK_STUDIO_TEST_MODE")
        os.environ["ARK_STUDIO_TEST_MODE"] = "1"
        try:
            registry = ToolRegistry(SCRIPT_ROOT / "resources" / "tools")
            self.assertIn("test.demo_progress", {tool["id"] for tool in registry.list()})
        finally:
            if before is None:
                os.environ.pop("ARK_STUDIO_TEST_MODE", None)
            else:
                os.environ["ARK_STUDIO_TEST_MODE"] = before

    def test_check_and_progress_contract_is_required(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "invalid.json"
            path.write_text(json.dumps({
                "schema_version": 1, "id": "test.invalid", "title": "Invalid", "description": "",
                "category": "test", "icon": "Wrench", "availability": "active", "parameter_mode": "none",
                "entrypoint": {"kind": "builtin", "handler": "demo_progress"}
            }), encoding="utf-8")
            with self.assertRaises(BrainError):
                ToolRegistry(Path(temporary))

    def test_parameter_mode_contract_is_required(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "invalid.json"
            path.write_text(json.dumps({
                "schema_version": 1, "id": "test.invalid", "title": "Invalid",
                "description": "", "category": "test", "icon": "Wrench",
                "availability": "active",
                "entrypoint": {"kind": "builtin", "handler": "demo_progress"},
                "check": {"handler": "always", "timeout_ms": 1000},
                "progress": {"mode": "event"}, "inputs": [],
            }), encoding="utf-8")
            with self.assertRaises(BrainError):
                ToolRegistry(Path(temporary))


if __name__ == "__main__":
    unittest.main()
