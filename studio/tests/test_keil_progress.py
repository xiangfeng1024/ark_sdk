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
import subprocess
import sys
import unittest
from unittest.mock import patch


SCRIPT_ROOT = Path(__file__).resolve().parents[1]
SDK_ROOT = SCRIPT_ROOT.parent
sys.path.insert(0, str(SCRIPT_ROOT))

from studio.tooling.keil_tools import _flash_model, build_work_count
from studio.tooling.probe_check import check_probe
from studio.tooling.project_config import load_app_config


class KeilProgressTests(unittest.TestCase):
    def test_full_and_incremental_build_counts_are_bounded(self) -> None:
        config = load_app_config(
            SDK_ROOT / "app" / "c8t6_microcar_soil" / "c8t6_microcar_soil.dts")
        total, full = build_work_count(config.keil_project, config.target, True)
        incremental_total, incremental = build_work_count(
            config.keil_project, config.target, False)

        self.assertGreater(total, 0)
        self.assertEqual(full, total)
        self.assertEqual(incremental_total, total)
        self.assertGreaterEqual(incremental, 0)
        self.assertLessEqual(incremental, total)

    def test_probe_timeout_is_reported_without_download(self) -> None:
        with patch("studio.tooling.probe_check.find_openocd", return_value=Path("openocd.exe")), patch(
            "studio.tooling.probe_check.subprocess.run",
            side_effect=subprocess.TimeoutExpired(["openocd"], 0.1),
        ):
            result = check_probe("dap", "STM32F103C8", 100)

        self.assertFalse(result.connected)
        self.assertIn("已终止", result.message)

    def test_flash_model_uses_firmware_size_target_flash_and_probe_rate(self) -> None:
        config = load_app_config(
            SDK_ROOT / "app" / "c8t6_microcar_soil" / "c8t6_microcar_soil.dts")
        dap = _flash_model(config.keil_project, config.target, "dap")
        stlink = _flash_model(config.keil_project, config.target, "stlink")

        self.assertGreater(dap[0], 0)
        self.assertGreaterEqual(dap[1], dap[0])
        self.assertGreater(dap[3], stlink[3])


if __name__ == "__main__":
    unittest.main()
