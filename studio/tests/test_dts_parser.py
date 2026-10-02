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

from studio.tooling.dts_parser import DtsReference, parse_dts


class DtsParserTests(unittest.TestCase):
    def test_standard_values_include_phandle_and_overlay(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "provider.dtsi").write_text('''
/ {
    bus {
        gpio0: gpio@0 {
            compatible = "ark,gpio";
            #gpio-cells = <2>;
            status = "okay";
        };
    };
};
''', encoding="utf-8")
            source = root / "demo.dts"
            source.write_text('''/dts-v1/;
/include/ "provider.dtsi"
&gpio0 { ark,value = <1 0x20>; };
/ {
    model = "Demo";
    device {
        compatible = "ark,device", "ark,fallback";
        gpios = <&gpio0 13 1>;
        bytes = [01 a5 ff];
        enabled;
    };
};
''', encoding="utf-8")
            document = parse_dts(source, root)
            provider = document.labels["gpio0"]
            device = document.find_path("/device")
            self.assertEqual(provider.cells("ark,value"), (1, 0x20))
            self.assertIsInstance(device.cells("gpios")[0], DtsReference)
            self.assertEqual(device.prop("bytes").value, bytes((1, 0xA5, 0xFF)))
            self.assertTrue(device.boolean("enabled"))

    def test_include_escape_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = root / "app"
            app.mkdir()
            outside = root / "outside.dtsi"
            outside.write_text("/ {};", encoding="utf-8")
            source = app / "demo.dts"
            source.write_text('/dts-v1/; /include/ "../outside.dtsi" / {};', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "escapes SDK/App"):
                parse_dts(source, app)

    def test_duplicate_label_and_unresolved_overlay_fail(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            duplicate = root / "duplicate.dts"
            duplicate.write_text(
                '/dts-v1/; / { x: a {}; x: b {}; };', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "duplicate DTS label"):
                parse_dts(duplicate, root)
            unresolved = root / "unresolved.dts"
            unresolved.write_text(
                '/dts-v1/; / {}; &missing { status = "okay"; };', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "unresolved DTS overlay"):
                parse_dts(unresolved, root)


if __name__ == "__main__":
    unittest.main()
