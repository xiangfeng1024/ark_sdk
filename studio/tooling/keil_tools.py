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

"""Shared helpers for Keil uVision command-line automation on Windows."""

from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
from typing import Callable
import xml.etree.ElementTree as ET

from studio.tooling.process_runner import hidden_process_kwargs


OutputHandler = Callable[[str], None]
CancelHandler = Callable[[], bool]
ProcessHandler = Callable[[subprocess.Popen[bytes]], None]


def _target_node(project: Path, target: str | None) -> ET.Element:
    root = ET.parse(project).getroot()
    targets = root.findall(".//Target")
    if target:
        selected = next(
            (node for node in targets if node.findtext("TargetName") == target),
            None,
        )
    else:
        selected = targets[0] if targets else None
    if selected is None:
        raise ValueError(f"Keil target does not exist: {target or '<default>'}")
    return selected


def _target_sources(project: Path, target: str | None) -> list[Path]:
    selected = _target_node(project, target)
    sources: list[Path] = []
    for file_node in selected.findall("./Groups/Group/Files/File"):
        file_type = (file_node.findtext("FileType") or "").strip()
        include = file_node.findtext("./FileOption/CommonProperty/IncludeInBuild")
        value = (file_node.findtext("FilePath") or "").strip()
        if file_type not in {"1", "2", "8"} or include == "0" or not value:
            continue
        path = Path(value.replace("\\", os.sep).replace("/", os.sep))
        if not path.is_absolute():
            path = project.parent / path
        sources.append(path.resolve())
    return sources


def _target_output_directory(project: Path, target: str | None) -> Path:
    selected = _target_node(project, target)
    value = (
        selected.findtext("./TargetOption/TargetCommonOption/OutputDirectory") or
        ""
    ).strip()
    path = Path(value.replace("\\", os.sep).replace("/", os.sep))
    if not path.is_absolute():
        path = project.parent / path
    return path.resolve()


def _dependency_requires_build(project: Path, output: Path, source: Path) -> bool:
    dependency = output / f"{source.stem}.d"
    object_file = output / f"{source.stem}.o"
    if not dependency.is_file() or not object_file.is_file():
        return True
    object_time = object_file.stat().st_mtime_ns
    for line in dependency.read_text(encoding="utf-8", errors="replace").splitlines():
        separator = line.find(": ")
        if separator < 0:
            continue
        value = line[separator + 2:].strip()
        if not value:
            continue
        item = Path(value.replace("\\", os.sep).replace("/", os.sep))
        if not item.is_absolute():
            item = project.parent / item
        try:
            if not item.is_file() or item.stat().st_mtime_ns > object_time:
                return True
        except OSError:
            return True
    return source.stat().st_mtime_ns > object_time if source.is_file() else True


def build_work_count(
    project: Path,
    target: str | None,
    full_rebuild: bool,
) -> tuple[int, int]:
    """Return total compilable units and units expected to rebuild now."""
    sources = _target_sources(project, target)
    if full_rebuild:
        return len(sources), len(sources)
    output = _target_output_directory(project, target)
    required = sum(
        1 for source in sources
        if _dependency_requires_build(project, output, source)
    )
    return len(sources), required


def _flash_model(project: Path, target: str | None, probe: str) -> tuple[int, int, float, float, float]:
    build_log = project.parent / ".keil" / "build.log"
    log_text = build_log.read_text(encoding="utf-8", errors="replace") if build_log.is_file() else ""
    size_match = re.search(
        r"Program Size:\s*Code=(\d+)\s+RO-data=(\d+)\s+RW-data=(\d+)",
        log_text,
        re.IGNORECASE,
    )
    firmware_size = sum(int(value) for value in size_match.groups()) if size_match else 0
    selected = _target_node(project, target)
    flash_text = selected.findtext(".//OnChipMemories/IROM/Size", default="0")
    flash_size = int(flash_text, 0)
    if firmware_size <= 0:
        firmware_size = min(max(flash_size, 1), 64 * 1024)
    rates = {
        "dap": (128 * 1024, 32 * 1024, 96 * 1024),
        "stlink": (192 * 1024, 64 * 1024, 160 * 1024),
    }
    erase_rate, program_rate, verify_rate = rates.get(probe, rates["dap"])
    erase_seconds = 0.25 + firmware_size / erase_rate
    program_seconds = 0.30 + firmware_size / program_rate
    verify_seconds = 0.15 + firmware_size / verify_rate
    return firmware_size, flash_size, erase_seconds, program_seconds, verify_seconds


def memory_usage_table(log_text: str, project: Path) -> str:
    match = re.search(
        r"Program Size:\s*Code=(\d+)\s+RO-data=(\d+)\s+RW-data=(\d+)\s+ZI-data=(\d+)",
        log_text,
        re.IGNORECASE,
    )
    if match is None:
        return ""

    code, ro_data, rw_data, zi_data = (int(value) for value in match.groups())
    tree = ET.parse(project)
    iram_size = tree.findtext(".//OnChipMemories/IRAM/Size", default="0")
    irom_size = tree.findtext(".//OnChipMemories/IROM/Size", default="0")
    sram_total = int(iram_size, 0)
    flash_total = int(irom_size, 0)
    flash_used = code + ro_data + rw_data
    sram_used = rw_data + zi_data

    def row(name: str, used: int, total: int) -> str:
        free = max(total - used, 0)
        percent = (used * 100.0 / total) if total else 0.0
        return f"| {name:<8} | {used:>8} | {total:>8} | {free:>8} | {percent:>6.2f}% |"

    return "\n".join(
        (
            "",
            "Firmware fields (bytes)",
            "+----------+----------+----------+----------+----------+",
            "| Code     | RO-data  | RW-data  | ZI-data  |          |",
            "+----------+----------+----------+----------+----------+",
            f"| {code:>8} | {ro_data:>8} | {rw_data:>8} | {zi_data:>8} |          |",
            "+----------+----------+----------+----------+----------+",
            "Memory usage (bytes)",
            "+----------+----------+----------+----------+---------+",
            "| Region   |     Used |    Total |     Free |   Usage |",
            "+----------+----------+----------+----------+---------+",
            row("Flash", flash_used, flash_total),
            row("SRAM", sram_used, sram_total),
            "+----------+----------+----------+----------+---------+",
            "",
        )
    )


def find_uv4(explicit: str | None = None) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit))

    for name in ("KEIL_UV4", "UV4_PATH"):
        value = os.environ.get(name)
        if value:
            candidates.append(Path(value))

    path_hit = shutil.which("UV4.exe")
    if path_hit:
        candidates.append(Path(path_hit))

    if sys.platform == "win32":
        try:
            import winreg

            registry_locations = (
                (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Keil\Products\MDK"),
                (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Keil\Products\MDK"),
                (winreg.HKEY_CURRENT_USER, r"SOFTWARE\Keil\Products\MDK"),
            )
            for hive, key_name in registry_locations:
                try:
                    with winreg.OpenKey(hive, key_name) as key:
                        arm_path = Path(winreg.QueryValueEx(key, "Path")[0])
                        candidates.append(arm_path.parent / "UV4" / "UV4.exe")
                except OSError:
                    continue
        except ImportError:
            pass

    candidates.extend(
        Path(value)
        for value in (
            r"C:\Keil_v5\UV4\UV4.exe",
            r"C:\Keil\UV4\UV4.exe",
            r"C:\Program Files\Keil_v5\UV4\UV4.exe",
            r"C:\Program Files (x86)\Keil_v5\UV4\UV4.exe",
        )
    )

    for candidate in candidates:
        candidate = candidate.expanduser().resolve()
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(
        "UV4.exe was not found. Install Keil MDK or set KEIL_UV4 to its full path."
    )


def run_uv4(
    uv4: Path,
    operation: str,
    project: Path,
    target: str | None,
    log_file: Path,
    on_output: OutputHandler | None = None,
    progress_range: tuple[int, int] | None = None,
    cancelled: CancelHandler | None = None,
    on_process: ProcessHandler | None = None,
    flash_probe: str = "dap",
) -> int:
    def emit(text: str) -> None:
        nonlocal output_at_line_start

        if on_output is not None:
            on_output(text)
        else:
            print(text, end="", flush=True)
        output_at_line_start = text.endswith(("\n", "\r"))

    def mapped_percent(percent: int) -> int:
        if progress_range is None:
            return percent
        start, end = progress_range
        return start + round((end - start) * min(max(percent, 0), 100) / 100)

    def emit_progress(percent: int, stage: str) -> None:
        nonlocal reported_percent

        value = mapped_percent(percent)
        if value > reported_percent:
            reported_percent = value
            prefix = "" if output_at_line_start else "\n"
            emit(f"{prefix}[ARK_PROGRESS] {value} {stage}\n")

    log_file.parent.mkdir(parents=True, exist_ok=True)
    log_file.write_bytes(b"")
    command = [str(uv4), operation, str(project)]
    if target:
        command.extend(["-t", target])
    command.extend(["-j0", "-o", str(log_file)])

    output_at_line_start = True
    reported_percent = -1
    observed_flash_text = ""
    build_total = 0
    build_expected = 0
    compiled_count = 0
    flash_stage = "connect"
    flash_stage_started = time.monotonic()
    flash_firmware_size = 0
    flash_total_size = 0
    erase_seconds = 1.0
    program_seconds = 1.0
    verify_seconds = 1.0
    if operation == "-f":
        emit_progress(0, "准备烧录")
        (flash_firmware_size, flash_total_size, erase_seconds,
         program_seconds, verify_seconds) = _flash_model(project, target, flash_probe)
        emit(
            f"[flash estimate] probe={flash_probe} firmware={flash_firmware_size} bytes "
            f"target_flash={flash_total_size} bytes erase={erase_seconds:.2f}s "
            f"program={program_seconds:.2f}s verify={verify_seconds:.2f}s\n"
        )
    elif operation in ("-b", "-r") and progress_range is not None:
        emit_progress(0, "构建命令已准备")
        build_total, build_expected = build_work_count(
            project, target, operation == "-r")
        emit(
            f"[build plan] compile={build_expected} total={build_total} "
            f"mode={'full' if operation == '-r' else 'incremental'}\n"
        )
        emit_progress(12, f"本次需要编译 {build_expected}/{build_total} 个文件")

    process = subprocess.Popen(
        command,
        cwd=project.parent,
        **hidden_process_kwargs(new_process_group=True),
    )
    if on_process is not None:
        on_process(process)
    if operation in ("-b", "-r") and progress_range is not None:
        emit_progress(3, "Keil进程已启动，等待构建日志")
    elif operation == "-f" and progress_range is not None:
        emit_progress(3, "Keil烧录进程已启动")
    log_position = 0
    encoding = "mbcs" if sys.platform == "win32" else "utf-8"
    while True:
        if log_file.exists():
            current_size = log_file.stat().st_size
            if current_size < log_position:
                log_position = 0
            if current_size > log_position:
                with log_file.open("rb") as stream:
                    stream.seek(log_position)
                    data = stream.read()
                log_position += len(data)
                chunk = data.decode(encoding, errors="replace").replace("\r\n", "\n").replace("\r", "\n")
                emit(chunk)
                if operation == "-f":
                    observed_flash_text += chunk
                    normalized = observed_flash_text.lower()
                    if 'load "' in normalized:
                        emit_progress(5, "加载镜像")
                    if "erase done" in normalized:
                        emit_progress(25, "擦除完成")
                        if flash_stage == "connect":
                            flash_stage = "program"
                            flash_stage_started = time.monotonic()
                    if "programming done" in normalized:
                        emit_progress(80, "编程完成")
                        if flash_stage != "verify":
                            flash_stage = "verify"
                            flash_stage_started = time.monotonic()
                    if "verify ok" in normalized:
                        emit_progress(100, "校验完成")
                        flash_stage = "complete"
                elif progress_range is not None:
                    normalized = chunk.lower()
                    emit_progress(8, "收到Keil构建日志")
                    if "using compiler" in normalized:
                        emit_progress(14, "编译工具链已加载")
                    if "build target" in normalized:
                        emit_progress(18, "正在分析构建目标")
                    compiled_lines = sum(
                        1 for line in chunk.splitlines()
                        if line.strip().lower().startswith(("compiling ", "assembling "))
                    )
                    if compiled_lines:
                        compiled_count += compiled_lines
                        if compiled_count > build_expected:
                            build_expected = max(build_total, compiled_count)
                        compile_percent = 20 + round(
                            50 * compiled_count / max(build_expected, 1))
                        emit_progress(
                            min(compile_percent, 70),
                            f"正在编译 {compiled_count}/{build_expected} 个文件",
                        )
                    if "linking" in normalized:
                        emit_progress(75, "正在链接固件")
                    if "program size:" in normalized:
                        emit_progress(84, "固件映像已生成")
                    if re.search(r"\b0\s+error\(s\)", normalized):
                        emit_progress(90, "构建结果为0错误")
        if operation == "-f" and progress_range is not None:
            stage_elapsed = time.monotonic() - flash_stage_started
            if flash_stage == "connect":
                fraction = min(stage_elapsed / max(erase_seconds, 0.1), 1.0)
                emit_progress(5 + round(17 * fraction), "正在连接并擦除目标Flash（估算）")
            elif flash_stage == "program":
                fraction = min(stage_elapsed / max(program_seconds, 0.1), 1.0)
                emit_progress(25 + round(52 * fraction), "正在烧录固件（估算）")
            elif flash_stage == "verify":
                fraction = min(stage_elapsed / max(verify_seconds, 0.1), 1.0)
                emit_progress(80 + round(16 * fraction), "正在校验固件（估算）")
        return_code = process.poll()
        if return_code is not None:
            break
        if cancelled is not None and cancelled():
            if os.name == "nt":
                subprocess.run(
                    ["taskkill", "/PID", str(process.pid), "/T", "/F"],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False,
                    **hidden_process_kwargs(),
                )
            else:
                process.terminate()
            process.wait()
            return 130
        time.sleep(0.05)

    if log_file.exists() and log_file.stat().st_size > log_position:
        with log_file.open("rb") as stream:
            stream.seek(log_position)
            chunk = stream.read().decode(encoding, errors="replace").replace("\r\n", "\n").replace("\r", "\n")
        emit(chunk)
        if operation == "-f":
            observed_flash_text += chunk
            normalized = observed_flash_text.lower()
            if "verify ok" in normalized:
                emit_progress(100, "校验完成")

    log_text = log_file.read_text(encoding="utf-8", errors="replace") if log_file.exists() else ""
    error_match = re.search(r"(\d+)\s+Error\(s\)", log_text, re.IGNORECASE)
    if return_code != 0 or (error_match and int(error_match.group(1)) != 0):
        if operation == "-f":
            emit(f"[ARK_PROGRESS] {max(reported_percent, 0)} 烧录失败\n")
        return return_code or 1
    if operation in ("-b", "-r"):
        summary = memory_usage_table(log_text, project)
        if summary:
            emit(summary)
        if progress_range is not None:
            emit_progress(96, "构建输出与内存信息已解析")
    return 0
