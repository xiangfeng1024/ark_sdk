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
from contextlib import redirect_stdout
import io
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch


SCRIPT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_ROOT))

from studio.tooling import serial_monitor


class _FakeSerialStream:
    def __init__(self) -> None:
        self.in_waiting = 0
        self.writes: list[bytes] = []

    def __enter__(self):
        return self

    def __exit__(self, _type, _value, _traceback) -> None:
        return None

    @staticmethod
    def read(_size: int) -> bytes:
        return b""

    def write(self, value: bytes) -> None:
        self.writes.append(value)

    @staticmethod
    def flush() -> None:
        return None


class SerialMonitorTests(unittest.TestCase):

    def test_auto_baud_uses_uart_cli_stream_dts(self) -> None:
        config = SCRIPT_ROOT.parent / "app" / "c8t6_ark_net" / "c8t6_ark_net.dts"

        baud = serial_monitor._resolve_baud("auto", config)

        self.assertEqual(baud, 921600)

    def test_interactive_console_has_no_tx_rx_status_echo(self) -> None:
        stream = _FakeSerialStream()
        serial_module = SimpleNamespace(Serial=lambda *_args, **_kwargs: stream)
        keys = iter(["h", "i", "\r", "\x03"])
        keyboard_module = SimpleNamespace(
            kbhit=lambda: True,
            getwch=lambda: next(keys),
        )
        output = io.StringIO()
        with patch.dict(sys.modules, {
                 "serial": serial_module,
                 "msvcrt": keyboard_module,
             }), \
             patch.object(serial_monitor, "_resolve_port", return_value="COM3"), \
             redirect_stdout(output):
            result = serial_monitor.run_interactive("COM3", 115200, "", None)

        self.assertEqual(result, 0)
        self.assertEqual(stream.writes, [b"h", b"i", b"\r"])
        self.assertNotIn("TX", output.getvalue())
        self.assertNotIn("RX", output.getvalue())

    def test_startup_command_appends_crlf_once(self) -> None:
        self.assertEqual(serial_monitor._startup_payload("help"), b"help\r\n")
        self.assertEqual(serial_monitor._startup_payload("help\n"), b"help\r\n")
        self.assertEqual(serial_monitor._startup_payload(""), b"")

if __name__ == "__main__":
    unittest.main()
