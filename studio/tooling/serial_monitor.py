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

"""Launch or run the ARK CREW SDK interactive serial monitor."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time

from studio.tooling.dts_parser import parse_dts


TERMINAL_READY_TIMEOUT_S = 5.0


def _emit_progress(percent: int, message: str) -> None:
    print(f"[ARK_PROGRESS] {percent} {message}", flush=True)


def _ports() -> list[str]:
    from serial.tools import list_ports

    return [port.device for port in list_ports.comports()]


def _resolve_port(value: str) -> str:
    ports = _ports()
    if value != "auto":
        if value not in ports:
            raise ValueError(f"串口不存在: {value}；当前={ports}")
        return value
    usb_ports = [port for port in ports if port.upper() != "COM1"]
    if usb_ports:
        return usb_ports[0]
    if ports:
        return ports[0]
    raise ValueError("未检测到串口")


def _resolve_dts_baud(config_path: Path) -> int:
    sdk_root = config_path.resolve().parents[2]
    document = parse_dts(config_path.resolve(), sdk_root)
    cli = next(
        (node for node in document.walk() if "uart_cli" in node.strings("compatible")),
        None,
    )
    if cli is None:
        raise ValueError(f"DTS中未找到uart_cli节点: {config_path}")
    stream_names = cli.strings("ark,stream-name")
    if len(stream_names) != 1:
        raise ValueError("uart_cli节点必须定义一个ark,stream-name")
    stream = next(
        (node for node in document.walk()
         if ("stream" in node.strings("compatible")) and
         (node.strings("ark,stream-name") == stream_names)),
        None,
    )
    if stream is None:
        raise ValueError(f"未找到CLI字节流: {stream_names[0]}")
    speeds = stream.cells("current-speed")
    if not speeds and stream.parent is not None:
        speeds = stream.parent.cells("current-speed")
    if (len(speeds) != 1) or not isinstance(speeds[0], int) or (speeds[0] <= 0):
        raise ValueError("CLI字节流或其UART父节点必须定义有效current-speed")
    return speeds[0]


def _resolve_baud(value: str, config_path: Path | None) -> int:
    if value.lower() != "auto":
        try:
            baud = int(value)
        except ValueError as exc:
            raise ValueError(f"无效波特率: {value}") from exc
        if baud <= 0:
            raise ValueError("波特率必须大于0")
        return baud
    if config_path is None:
        raise ValueError("--baud auto需要同时提供--config APP_DTS")
    return _resolve_dts_baud(config_path)


def _write_status(status_file: Path | None, value: str) -> None:
    if status_file is not None:
        status_file.write_text(value, encoding="utf-8")


def _serial_reader(stream, stopped: threading.Event) -> None:
    while not stopped.is_set():
        data = stream.read(max(stream.in_waiting, 1))
        if data:
            print(data.decode("utf-8", errors="replace"), end="", flush=True)


def _serial_keyboard_send(stream, stopped: threading.Event) -> None:
    import msvcrt

    ansi_keys = {
        "H": b"\x1b[A",
        "P": b"\x1b[B",
        "M": b"\x1b[C",
        "K": b"\x1b[D",
    }
    while not stopped.is_set():
        if not msvcrt.kbhit():
            time.sleep(0.01)
            continue
        character = msvcrt.getwch()
        if character == "\x03":
            stopped.set()
            break
        if character in {"\x00", "\xe0"}:
            payload = ansi_keys.get(msvcrt.getwch(), b"")
        else:
            payload = character.encode("utf-8")
        if payload:
            stream.write(payload)
            stream.flush()


def _startup_payload(send: str) -> bytes:
    if not send:
        return b""
    return send.rstrip("\r\n").encode("utf-8") + b"\r\n"


def run_interactive(port_value: str, baud: int, send: str, status_file: Path | None) -> int:
    try:
        import serial

        port = _resolve_port(port_value)
        if baud <= 0:
            raise ValueError("波特率必须大于0")
        with serial.Serial(port, baud, timeout=0.05) as stream:
            _write_status(status_file, f"READY\n{port}\n{baud}")
            print("=" * 68)
            print(f"ARK CREW SDK 串口终端已连接: {port} @ {baud}")
            print("按键立即发送且本地不回显；仅显示设备实际返回的数据。")
            print("按Ctrl+C或关闭窗口退出。")
            print("=" * 68)
            if send:
                payload = _startup_payload(send)
                stream.write(payload)
                stream.flush()

            stopped = threading.Event()
            reader = threading.Thread(
                target=_serial_reader, args=(stream, stopped), daemon=True)
            reader.start()
            try:
                _serial_keyboard_send(stream, stopped)
            except KeyboardInterrupt:
                stopped.set()
            finally:
                stopped.set()
                reader.join(timeout=0.2)
        return 0
    except (ImportError, OSError, ValueError) as exc:
        _write_status(status_file, f"ERROR\n{exc}")
        print(f"串口打开失败: {exc}")
        return 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--interactive", action="store_true")
    parser.add_argument("--port", default="auto")
    parser.add_argument("--baud", default="auto")
    parser.add_argument("--config", type=Path)
    parser.add_argument("--send", default="")
    parser.add_argument("--status-file", type=Path)
    args = parser.parse_args()
    try:
        import serial  # noqa: F401

        port = _resolve_port(args.port)
        baud = _resolve_baud(args.baud, args.config)
        _emit_progress(10, "检查串口设备、pyserial和终端环境")
        if args.check:
            print(f"serial ready: {port} @ {baud}")
            _emit_progress(100, "串口终端环境检查通过")
            return 0
        if args.interactive:
            return run_interactive(
                port, baud, args.send, args.status_file)
        raise ValueError("请使用Studio内嵌串口终端，或为命令行指定--interactive")
    except (ImportError, OSError, TimeoutError, ValueError) as exc:
        if args.status_file is not None:
            _write_status(args.status_file, f"ERROR\n{exc}")
        print(f"error: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
