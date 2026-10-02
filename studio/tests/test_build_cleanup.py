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
from unittest.mock import patch


SCRIPT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_ROOT))

from studio.tooling.build_cleanup import CleanupCancelled, create_cleanup_plan, execute_cleanup_plan
from studio.tooling.project_config import CubeProjectBinding


class BuildCleanupTests(unittest.TestCase):
    def _fixture(self, root: Path) -> Path:
        app_dir = root / "c8t6_app"
        cube_dir = root / "c8t6_app"
        project_dir = cube_dir / "MDK-ARM"
        app_dir.mkdir()
        (project_dir / "Objects").mkdir(parents=True)
        (project_dir / "Listings").mkdir()
        (project_dir / ".keil").mkdir()
        (project_dir / "Objects" / "demo.axf").write_bytes(b"axf")
        (project_dir / "Objects" / "demo.o").write_bytes(b"object")
        (project_dir / "Listings" / "demo.map").write_bytes(b"map")
        (project_dir / ".keil" / "build.log").write_text("log", encoding="utf-8")
        (cube_dir / "c8t6_app.ioc").write_text(
            "ProjectManager.ProjectName=c8t6_app\n", encoding="utf-8")
        project = project_dir / "c8t6_app.uvprojx"
        project.write_text("""<Project><Targets><Target><TargetName>c8t6_app</TargetName><TargetOption><TargetCommonOption><OutputDirectory>Objects</OutputDirectory><ListingPath>Listings</ListingPath></TargetCommonOption></TargetOption></Target></Targets></Project>""", encoding="utf-8")
        config = app_dir / "c8t6_app.dts"
        config.write_text('''/dts-v1/;
/ {
    compatible = "c8t6_test", "stm32f103c8";
    sys {};
    software {};
};
''', encoding="utf-8")
        binding = CubeProjectBinding(
            cube_dir, cube_dir / "c8t6_app.ioc", project,
            "c8t6_app", "../ark_sdk")
        return config, binding

    def test_cleanup_reports_real_scan_and_delete_progress(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config, binding = self._fixture(Path(temporary))
            events: list[tuple[int, str, str]] = []
            with patch("studio.tooling.project_config.infer_cube_project", return_value=binding):
                plan = create_cleanup_plan(config, lambda *value: events.append(value))
            self.assertEqual(len(plan.files), 4)
            execute_cleanup_plan(plan, lambda *value: events.append(value))
            self.assertFalse((Path(temporary) / "c8t6_app" / "MDK-ARM" / "Objects").exists())
            self.assertTrue(any(percent == 20 and stage == "scan" for percent, stage, _ in events))
            self.assertTrue(any(percent == 92 and stage == "clean" for percent, stage, _ in events))

    def test_cleanup_cancellation_stops_without_generic_process_error(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config, binding = self._fixture(Path(temporary))
            with patch("studio.tooling.project_config.infer_cube_project", return_value=binding):
                plan = create_cleanup_plan(config)
            with self.assertRaises(CleanupCancelled):
                execute_cleanup_plan(plan, cancelled=lambda: True)
            self.assertTrue((Path(temporary) / "c8t6_app" / "MDK-ARM" / "Objects" / "demo.axf").is_file())


if __name__ == "__main__":
    unittest.main()
