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

from studio.tooling.project_config import (
    _is_managed_sdk_group,
    _managed_sdk_path_prefixes,
    infer_cube_project,
)


class ProjectDiscoveryTests(unittest.TestCase):
    def test_managed_sdk_cleanup_covers_pre_migration_names(self) -> None:
        old_group = "X" + "Y_SDK/Components"
        old_directory = "x" + "y_sdk"

        self.assertTrue(_is_managed_sdk_group("ARK_SDK/App"))
        self.assertTrue(_is_managed_sdk_group(old_group))
        self.assertEqual(
            _managed_sdk_path_prefixes("../../ark_sdk"),
            ("../../../ark_sdk/", "../../../" + old_directory + "/"),
        )

    @staticmethod
    def _cube(root: Path, name: str, target: str, synced_app: str = "") -> None:
        cube = root / "projects" / name
        keil = cube / "MDK-ARM"
        keil.mkdir(parents=True)
        (cube / f"{name}.ioc").write_text(
            f"ProjectManager.ProjectName={name}\n"
            f"ProjectManager.ProjectFileName={name}.ioc\n", encoding="utf-8")
        app_group = ""
        if synced_app:
            app_group = (
                "<Groups><Group><GroupName>ARK_SDK/App</GroupName><Files><File>"
                f"<FilePath>../../../ark_sdk/app/{synced_app}/src/app_main.c</FilePath>"
                "</File></Files></Group></Groups>")
        (keil / f"{name}.uvprojx").write_text(
            "<Project><Targets><Target>"
            f"<TargetName>{target}</TargetName>{app_group}"
            "</Target></Targets></Project>", encoding="utf-8")

    def test_identical_prefixed_name_resolves_target_and_sdk(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sdk = root / "ark_sdk"
            sdk.mkdir()
            self._cube(root, "c8t6_demo", "c8t6_demo")
            with patch("studio.tooling.project_config.SDK_ROOT", sdk):
                binding = infer_cube_project("c8t6_demo", root)
            self.assertEqual(binding.ioc.name, "c8t6_demo.ioc")
            self.assertEqual(binding.target, "c8t6_demo")
            self.assertEqual(binding.sdk_link, "../../ark_sdk")

    def test_duplicate_identical_ioc_names_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sdk = root / "ark_sdk"
            sdk.mkdir()
            self._cube(root, "c8t6_demo", "c8t6_demo")
            duplicate = root / "another" / "c8t6_demo"
            keil = duplicate / "MDK-ARM"
            keil.mkdir(parents=True)
            (duplicate / "c8t6_demo.ioc").write_text(
                "ProjectManager.ProjectName=c8t6_demo\n"
                "ProjectManager.ProjectFileName=c8t6_demo.ioc\n",
                encoding="utf-8")
            (keil / "c8t6_demo.uvprojx").write_text(
                "<Project><Targets><Target><TargetName>c8t6_demo</TargetName>"
                "</Target></Targets></Project>", encoding="utf-8")
            with patch("studio.tooling.project_config.SDK_ROOT", sdk):
                with self.assertRaisesRegex(ValueError, "matches multiple"):
                    infer_cube_project("c8t6_demo", root)

    def test_differently_named_legacy_project_is_not_inferred(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sdk = root / "ark_sdk"
            sdk.mkdir()
            self._cube(root, "board_old_name", "board_old_name", "c8t6_demo")
            with patch("studio.tooling.project_config.SDK_ROOT", sdk):
                with self.assertRaisesRegex(ValueError, "expected the same name"):
                    infer_cube_project("c8t6_demo", root)

    def test_target_must_match_app_or_ioc_name(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sdk = root / "ark_sdk"
            sdk.mkdir()
            self._cube(root, "c8t6_demo", "unexpected")
            with patch("studio.tooling.project_config.SDK_ROOT", sdk):
                with self.assertRaisesRegex(ValueError, "target must equal"):
                    infer_cube_project("c8t6_demo", root)


if __name__ == "__main__":
    unittest.main()
