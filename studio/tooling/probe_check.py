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

"""Read-only CMSIS-DAP/ST-Link and target connectivity checks."""

from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import json
from pathlib import Path
import shutil
import subprocess
import sys

from studio.tooling.process_runner import hidden_process_kwargs


PROBE_INTERFACES = {
    "dap": "interface/cmsis-dap.cfg",
    "stlink": "interface/stlink.cfg",
}
TARGET_CONFIGS = {
    "STM32F103C8": "target/stm32f1x.cfg",
    "GD32F103C8": "target/stm32f1x.cfg",
}


@dataclass(frozen=True)
class ProbeCheckResult:
    probe: str
    available: bool
    connected: bool
    message: str
    backend: str
    duration_ms: int
    output: str


def find_openocd() -> Path | None:
    hit = shutil.which("openocd") or shutil.which("openocd.exe")
    if hit:
        return Path(hit).resolve()
    if sys.platform == "win32":
        packages = Path.home() / "AppData" / "Local" / "Microsoft" / "WinGet" / "Packages"
        candidates = sorted(packages.glob("xpack-dev-tools.openocd-*/*/bin/openocd.exe"))
        if candidates:
            return candidates[-1].resolve()
    return None


def check_probe(
    probe: str,
    device: str,
    timeout_ms: int = 1800,
) -> ProbeCheckResult:
    import time

    started = time.monotonic()
    interface = PROBE_INTERFACES.get(probe)
    target = TARGET_CONFIGS.get(device.upper())
    openocd = find_openocd()
    if interface is None:
        return ProbeCheckResult(
            probe, False, False, f"不支持的探针类型: {probe}", "", 0, "")
    if target is None:
        return ProbeCheckResult(
            probe, False, False, f"没有目标芯片检查配置: {device}", "", 0, "")
    if openocd is None:
        return ProbeCheckResult(
            probe, False, False, "未找到OpenOCD，无法验证目标板连接", "", 0, "")

    command = [
        str(openocd),
        "-f", interface,
        "-f", target,
        "-c",
        "gdb port disabled; tcl port disabled; telnet port disabled; "
        "transport select swd; adapter speed 1000; init; targets; shutdown",
    ]
    try:
        result = subprocess.run(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=max(timeout_ms, 100) / 1000,
            check=False,
            **hidden_process_kwargs(),
        )
        output = result.stdout.strip()
    except subprocess.TimeoutExpired as exc:
        output = (exc.stdout or "") if isinstance(exc.stdout, str) else ""
        duration_ms = round((time.monotonic() - started) * 1000)
        return ProbeCheckResult(
            probe, True, False,
            f"探针连接检查超过{timeout_ms}ms，已终止", str(openocd),
            duration_ms, output,
        )
    except OSError as exc:
        duration_ms = round((time.monotonic() - started) * 1000)
        return ProbeCheckResult(
            probe, False, False, f"无法启动OpenOCD: {exc}", str(openocd),
            duration_ms, "",
        )

    normalized = output.lower()
    if probe == "dap":
        available = "cmsis-dap: interface ready" in normalized
    else:
        available = "st-link" in normalized or "stlink" in normalized
    connected = (
        result.returncode == 0 and
        "examination succeed" in normalized and
        ("dpidr" in normalized or "cortex-m" in normalized)
    )
    duration_ms = round((time.monotonic() - started) * 1000)
    if connected:
        detail = next(
            (line.strip() for line in output.splitlines() if "processor detected" in line.lower()),
            "SWD目标连接成功",
        )
        message = f"{probe.upper()}已连接目标板：{detail}"
    elif available:
        message = f"{probe.upper()}探针存在，但未连接到目标板"
    else:
        message = f"未检测到可用的{probe.upper()}探针"
    return ProbeCheckResult(
        probe, available, connected, message, str(openocd), duration_ms, output)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Run the read-only connectivity check")
    parser.add_argument("--probe", choices=sorted(PROBE_INTERFACES), default="dap")
    parser.add_argument("--device", default="STM32F103C8")
    parser.add_argument("--timeout-ms", type=int, default=1800)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    if not args.check:
        parser.error("--check is required; this script never downloads firmware")
    result = check_probe(args.probe, args.device, args.timeout_ms)
    if args.json:
        print(json.dumps(asdict(result), ensure_ascii=False, indent=2))
    else:
        print(result.message)
        if result.output:
            print(result.output)
    return 0 if result.connected else 1


if __name__ == "__main__":
    raise SystemExit(main())
