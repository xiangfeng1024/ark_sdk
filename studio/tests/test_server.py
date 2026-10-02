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
from pathlib import Path
import subprocess
import sys
import unittest

from studio.brain.server import json_safe


SCRIPT_ROOT = Path(__file__).resolve().parents[1]
SDK_ROOT = SCRIPT_ROOT.parent


class ServerTests(unittest.TestCase):
    def test_json_safe_replaces_isolated_surrogates(self) -> None:
        value = json_safe({"message": "bad\udc80path", "items": ["ok\ud800"]})
        encoded = json.dumps(value, ensure_ascii=False).encode("utf-8")
        self.assertIn("bad\ufffdpath", encoded.decode("utf-8"))
        self.assertIn("ok\ufffd", encoded.decode("utf-8"))

    def test_ping_protocol(self) -> None:
        process = subprocess.Popen(
            [sys.executable, "-u", "-m", "studio.brain.server", "--workspace", str(SDK_ROOT)],
            cwd=SDK_ROOT,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
        )
        assert process.stdin is not None and process.stdout is not None
        ready = json.loads(process.stdout.readline())
        self.assertEqual(ready["event"], "system.ready")
        process.stdin.write(json.dumps({"id": "1", "method": "ping", "params": {}}) + "\n")
        process.stdin.flush()
        response = {}
        while response.get("id") != "1":
            response = json.loads(process.stdout.readline())
        self.assertTrue(response["result"]["ok"])
        process.stdin.write(json.dumps({"id": "2", "method": "registry.list", "params": {}}) + "\n")
        process.stdin.flush()
        response = {}
        while response.get("id") != "2":
            response = json.loads(process.stdout.readline())
        titles = {item["id"]: item["title"] for item in response["result"]}
        self.assertEqual(titles["dts.check"], "检查DTS")
        process.stdin.close()
        process.wait(timeout=3)
        process.stdout.close()
        assert process.stderr is not None
        process.stderr.close()


if __name__ == "__main__":
    unittest.main()
