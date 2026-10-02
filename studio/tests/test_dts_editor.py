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
from types import SimpleNamespace
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


SCRIPT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_ROOT))

from studio.tooling import dts_editor


class DtsEditorTests(unittest.TestCase):
    def test_desktop_editor_does_not_inherit_brain_output_pipe(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config_path = Path(temporary) / "app.dts"
            config_path.write_text("/dts-v1/; / {};", encoding="utf-8")
            config = SimpleNamespace(path=config_path)
            captured: dict[str, object] = {}

            def launch(command, **kwargs):
                captured["command"] = command
                captured["kwargs"] = kwargs
                return SimpleNamespace()

            with patch.object(sys, "argv", [
                     "studio.tooling.dts_editor.py", "--config", str(config_path), "--open",
                 ]), \
                 patch.dict("os.environ", {"ARK_STUDIO_TEST_MODE": "0"}), \
                 patch.object(dts_editor, "load_app_config", return_value=config), \
                 patch.object(
                     dts_editor,
                     "generate_for_app",
                     return_value=(Path("generated.h"), Path("generated.c"), False),
                 ), \
                 patch.object(dts_editor.shutil, "which", return_value="code.exe"), \
                 patch.object(dts_editor.subprocess, "Popen", side_effect=launch):
                result = dts_editor.main()

            self.assertEqual(result, 0)
            self.assertIs(captured["kwargs"]["stdin"], subprocess.DEVNULL)
            self.assertIs(captured["kwargs"]["stdout"], subprocess.DEVNULL)
            self.assertIs(captured["kwargs"]["stderr"], subprocess.DEVNULL)
            self.assertTrue(captured["kwargs"]["close_fds"])


if __name__ == "__main__":
    unittest.main()
