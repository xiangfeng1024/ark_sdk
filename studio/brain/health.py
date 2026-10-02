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

"""Passive workspace, toolchain and device enumeration checks."""

from __future__ import annotations

from pathlib import Path
import platform
import shutil
from typing import Any

from studio.tooling.keil_tools import find_uv4
from studio.tooling.project_config import detect_synced_app, load_app_config
from studio.tooling.ark_dts import generate_for_app

from .workspace import WorkspaceService


def _item(check_id: str, title: str, status: str, message: str, remediation: str = "") -> dict[str, Any]:
    return {
        "id": check_id,
        "title": title,
        "status": status,
        "message": message,
        "remediation": remediation,
    }


class HealthService:
    def __init__(self, workspace: WorkspaceService) -> None:
        self.workspace = workspace

    def scan(self) -> dict[str, Any]:
        result: list[dict[str, Any]] = []
        snapshot = self.workspace.snapshot()
        config_path = Path(snapshot["configPath"])
        config = load_app_config(config_path)
        settings = self.workspace.settings.get()
        result.append(_item("workspace", "SDK工作区", "ready", str(self.workspace.root)))
        result.append(_item("python", "Python", "ready", f"{platform.python_implementation()} {platform.python_version()}"))
        try:
            uv4 = find_uv4(None)
            result.append(_item("keil", "Keil uVision", "ready", str(uv4)))
        except FileNotFoundError as exc:
            result.append(_item("keil", "Keil uVision", "error", str(exc), "在设置中选择UV4.exe。"))
        result.append(_item(
            "project", "CubeMX/Keil工程", "ready" if config.keil_project.is_file() else "error",
            f"IOC={config.ioc_path}；Keil={config.keil_project}",
            "保持App、DTS、Cube目录、IOC、Keil工程和Target完全同名。",
        ))
        try:
            _header, _source, stale = generate_for_app(config_path, check=True)
            result.append(_item(
                "dts", "DTS配置", "warning" if stale else "ready",
                "生成文件已过期" if stale else "DTS与生成C/H一致",
                "运行“生成DTS”更新生成文件。" if stale else "",
            ))
        except (OSError, ValueError) as exc:
            result.append(_item("dts", "DTS配置", "error", str(exc), "修正App DTS。"))
        detected = detect_synced_app(config.keil_project) if config.keil_project.is_file() else None
        result.append(_item(
            "synced", "Keil App同步", "ready" if detected == config.app else "warning",
            f"已同步：{detected or '未识别'}；工具选择：{config.app}",
            "执行“同步Keil”后再编译或烧录。" if detected != config.app else "",
        ))
        ports: list[dict[str, str]] = []
        try:
            from serial.tools import list_ports
            ports = [
                {"device": port.device, "description": port.description or "", "hwid": port.hwid or ""}
                for port in list_ports.comports()
            ]
        except ImportError:
            pass
        result.append(_item(
            "serial", "串口设备", "ready" if ports else "warning",
            "、".join(port["device"] for port in ports) if ports else "未检测到串口",
            "连接调试器或串口设备，或安装pyserial。" if not ports else "",
        ))
        probe_words = ("cmsis-dap", "daplink", "st-link", "stlink")
        probes = [port for port in ports if any(word in (port["description"] + port["hwid"]).lower() for word in probe_words)]
        result.append(_item(
            "probe", "下载探针", "ready" if probes else "unknown",
            "、".join(item["description"] or item["device"] for item in probes) if probes else "未被动识别；执行烧录时再连接验证",
        ))
        return {"checks": result, "ports": ports, "timestamp": __import__("time").time()}
