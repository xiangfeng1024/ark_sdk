# SPDX-FileCopyrightText: Copyright (c) 2026 Ark Crew. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Ark-Crew-NonCommercial-1.0

"""Deterministic offscreen captures for Studio visual review."""

from __future__ import annotations

import os
from pathlib import Path
from unittest.mock import patch

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtTest import QTest
from PySide6.QtGui import QFont, QFontDatabase
from PySide6.QtWidgets import QApplication

from studio.app import ArkStyle, MainWindow
from studio.icons import ASSET_ROOT
from studio.theme import STYLESHEET


def capture(output: Path) -> None:
    app = QApplication.instance() or QApplication([])
    font_id = QFontDatabase.addApplicationFont(str(ASSET_ROOT / "fonts" / "NotoSansSC-VF.ttf"))
    families = QFontDatabase.applicationFontFamilies(font_id) if font_id >= 0 else []
    font = QFont(families[0] if families else "Microsoft YaHei UI", 10)
    font.setHintingPreference(QFont.HintingPreference.PreferFullHinting)
    app.setFont(font)
    app.setStyle(ArkStyle("Fusion"))
    app.setStyleSheet(STYLESHEET)
    with patch("studio.app.BrainClient.start"):
        window = MainWindow(Path.cwd())
    workspace = {
        "opened": True,
        "path": str(Path.cwd()),
        "activeApp": "c8t6_ark_net",
        "apps": ["c8t6_ark_net", "c8t6_microcar_soil"],
    }
    checks = {
        "state": "succeeded",
        "items": [{"status": "ready"} for _ in range(23)] + [{"status": "warning"}],
        "tools": {tool_id: {"available": True, "message": "环境与依赖均已就绪"} for tool_id in ("dts.check", "dts.generate", "keil.sync", "keil.build.incremental", "keil.build.full", "keil.flash.only")},
    }
    tools = [
        {"id": "dts.check", "title": "检查 DTS", "description": "只读校验当前 App 的设备树配置与生成文件。", "category": "配置", "parameterMode": "none"},
        {"id": "dts.generate", "title": "生成 DTS", "description": "校验配置并生成固件编译所需的只读数据。", "category": "配置", "parameterMode": "none"},
        {"id": "keil.sync", "title": "同步 Keil", "description": "同步 App、HAL 与组件源文件到当前 Keil 工程。", "category": "构建", "parameterMode": "none"},
        {"id": "keil.build.incremental", "title": "增量编译", "description": "编译发生变化的文件并检查固件产物。", "category": "构建", "parameterMode": "none"},
        {"id": "keil.build.full", "title": "全量编译", "description": "重新编译当前 target 并分析固件空间。", "category": "构建", "parameterMode": "none"},
        {"id": "keil.flash.only", "title": "烧录现有固件", "description": "使用当前探针烧录已生成的固件。", "category": "烧录", "parameterMode": "fixed"},
    ]
    task = {
        "id": "capture-task",
        "toolId": "dts.check",
        "title": "检查 DTS",
        "app": "c8t6_ark_net",
        "state": "succeeded",
        "stage": "校验完成",
        "percent": 100,
        "createdAt": "2026-09-08T10:20:00+08:00",
        "startedAt": "2026-09-08T10:20:00+08:00",
        "finishedAt": "2026-09-08T10:20:01.280+08:00",
        "message": "DTS 与生成文件一致",
        "logs": [
            {"timestamp": "2026-09-08T10:20:00+08:00", "level": "info", "source": "dts", "text": "读取 c8t6_ark_net.dts"},
            {"timestamp": "2026-09-08T10:20:01+08:00", "level": "success", "source": "dts", "text": "配置、phandle 与资源检查通过"},
        ],
        "artifacts": [{"name": "ark_dts_generated.c", "kind": "C", "relativePath": "app/c8t6_ark_net/src/ark_dts_generated.c"}],
    }
    window.workspace = workspace
    window.sdk_check = checks
    window.tools = tools
    window.tasks = [task]
    window.workbench.set_workspace(workspace, checks)
    window.workbench.set_tools(tools, checks["tools"])
    window.workbench.set_recent_tasks([task])
    window.console.set_task(task)
    window.resize(1440, 900)
    window.show()
    window.set_page("workbench")
    QTest.qWait(320)
    output.mkdir(parents=True, exist_ok=True)
    window.grab().save(str(output / "workbench-1440x900.png"))
    window.resize(1080, 680)
    QTest.qWait(80)
    window.grab().save(str(output / "workbench-1080x680.png"))
    window._toggle_global_logs(True)
    window._studio_log("error", "studio", "示例：工作区配置缺失，请打开设置并重新选择 SDK 工作区")
    QTest.qWait(30)
    window.grab().save(str(output / "global-log-1080x680.png"))
    window._closing = True
    window.close()


if __name__ == "__main__":
    capture(Path("studio/build/review-latest"))
