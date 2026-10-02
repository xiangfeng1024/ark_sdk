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

from studio.tooling.encoding_convert import convert, scan


class EncodingConvertTests(unittest.TestCase):
    def test_scan_and_convert_gb2312_to_utf8(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("app", "component", "script"):
                (root / name).mkdir()
            source = root / "component" / "demo.c"
            source.write_bytes("/* 中文 */\n".encode("gb2312"))
            items = scan(root)
            self.assertEqual(items[0].encoding, "gb2312")
            with patch("studio.tooling.encoding_convert.backup_root", return_value=root / "backup"):
                code, report = convert(root, "utf-8")
            self.assertEqual(code, 0)
            self.assertEqual(source.read_text(encoding="utf-8"), "/* 中文 */\n")
            self.assertTrue(report["backup"])

    def test_utf8_needs_no_confirmation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("app", "component", "script"):
                (root / name).mkdir()
            (root / "app" / "demo.dts").write_text("/ {};\n", encoding="utf-8")
            code, report = convert(root, "utf-8")
            self.assertEqual(code, 0)
            self.assertEqual(report["convertible"], [])

    def test_utf8_to_gb2312_converts_without_confirmation_flag(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "demo.c"
            source.write_bytes("/* 编码转换 */\n".encode("utf-8"))
            with patch("studio.tooling.encoding_convert.backup_root", return_value=root / "backup"):
                code, report = convert(root, "gb2312")
            self.assertEqual(code, 0)
            self.assertEqual(source.read_bytes().decode("gb2312"), "/* 编码转换 */\n")
            self.assertTrue(report["backup"])


if __name__ == "__main__":
    unittest.main()
