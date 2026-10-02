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

"""Post-execution validation for core Studio tools."""

from __future__ import annotations

from pathlib import Path
import re
from typing import Any
import xml.etree.ElementTree as ET

from studio.tooling.project_config import detect_synced_app, load_app_config
from studio.tooling.ark_dts import generate_for_app
from studio.tooling.build_cleanup import create_cleanup_plan


def _result(name: str, passed: bool, message: str) -> dict[str, Any]:
    return {"name": name, "passed": passed, "message": message}


def _target_output(config) -> Path:
    root = ET.parse(config.keil_project).getroot()
    for node in root.findall(".//Target"):
        if node.findtext("TargetName") == config.target:
            value = node.findtext("./TargetOption/TargetCommonOption/OutputDirectory") or ""
            return (config.keil_project.parent / value).resolve()
    return config.keil_project.parent


def _target_image(config) -> Path:
    root = ET.parse(config.keil_project).getroot()
    for node in root.findall(".//Target"):
        if node.findtext("TargetName") == config.target:
            output = node.findtext("./TargetOption/TargetCommonOption/OutputDirectory") or ""
            name = (node.findtext("./TargetOption/TargetCommonOption/OutputName") or config.target).strip()
            return (config.keil_project.parent / output / f"{name}.axf").resolve()
    return (_target_output(config) / f"{config.target}.axf").resolve()


def run_validators(names: tuple[str, ...], config_path: Path, output: str) -> list[dict[str, Any]]:
    config = load_app_config(config_path)
    results: list[dict[str, Any]] = []
    for name in names:
        if name == "dts_current":
            try:
                _header, _source, stale = generate_for_app(config_path, check=True)
                results.append(_result(name, not stale, "DTS生成文件已同步" if not stale else "DTS生成文件仍过期"))
            except (OSError, ValueError) as exc:
                results.append(_result(name, False, str(exc)))
        elif name == "synced_app":
            detected = detect_synced_app(config.keil_project)
            results.append(_result(name, detected == config.app, f"Keil App={detected or 'unknown'}"))
        elif name == "build_output":
            log_path = config.keil_project.parent / ".keil" / "build.log"
            log = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else output
            errors = re.search(r"(\d+)\s+Error\(s\)", log, re.IGNORECASE)
            axf = _target_image(config)
            passed = bool(errors and int(errors.group(1)) == 0 and axf.is_file())
            results.append(_result(name, passed, f"errors={errors.group(1) if errors else 'unknown'}, axf={axf.is_file()}"))
        elif name == "flash_complete":
            log_path = config.keil_project.parent / ".keil" / "flash.log"
            log = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else output
            verify = "verify ok" in log.lower()
            running = "application running" in log.lower()
            results.append(_result(name, verify and running, f"verify={verify}, running={running}"))
        elif name == "clean_complete":
            plan = create_cleanup_plan(config_path)
            results.append(_result(
                name, not plan.files,
                "构建输出、列表和工具日志已清除" if not plan.files else f"仍存在{len(plan.files)}个构建产物",
            ))
        else:
            results.append(_result(name, False, f"unknown validator: {name}"))
    return results
