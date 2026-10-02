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
import unittest

SCRIPT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_ROOT))

from studio.brain.settings import HistoryStore, SettingsStore


class SettingsTests(unittest.TestCase):
    def test_log_timestamps_can_be_disabled(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            store = SettingsStore(Path(temporary))
            self.assertTrue(store.get()["log_timestamps"])
            store.update({"log_timestamps": False})
            self.assertFalse(SettingsStore(Path(temporary)).get()["log_timestamps"])

    def test_tool_params_are_isolated_by_workspace_app_and_tool(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            store = SettingsStore(Path(temporary))
            store.set_tool_params("C:/sdk-a", "blinky", "keil.build", {"probe": "dap"})
            store.set_tool_params("C:/sdk-a", "cube3d", "keil.build", {"probe": "stlink"})
            self.assertEqual(store.get_tool_params("C:/sdk-a", "blinky", "keil.build"), {"probe": "dap"})
            self.assertEqual(store.get_tool_params("C:/sdk-a", "cube3d", "keil.build"), {"probe": "stlink"})
            self.assertEqual(store.get_tool_params("C:/sdk-b", "blinky", "keil.build"), {})

    def test_structured_history_and_legacy_log_compatibility(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            settings = SettingsStore(Path(temporary))
            history = HistoryStore(settings)
            record = {"id": "new", "state": "succeeded", "title": "New"}
            history.append(record, [{"sequence": 1, "timestamp": "now", "level": "success", "source": "test", "text": "ok\n"}])
            loaded = history.load()[0]
            self.assertEqual(history.read_logs(loaded)[0]["level"], "success")
            legacy = settings.root / "logs" / "legacy.log"
            legacy.write_text("old line\n", encoding="utf-8")
            self.assertEqual(history.read_logs({"logPath": str(legacy)})[0]["level"], "info")


if __name__ == "__main__":
    unittest.main()
