# SPDX-FileCopyrightText: Copyright (c) 2026 Ark Crew. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Ark-Crew-NonCommercial-1.0

"""Focused Qt widget tests for the native Studio."""

from __future__ import annotations

import os
from pathlib import Path
from unittest.mock import patch

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtCore import Qt
from PySide6.QtGui import QInputMethodEvent
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication, QWidget

from studio.app import ArtifactsPage, ElidedLabel, FadeStackedWidget, GlobalLogPanel, LogView, MainWindow, RecentPage, SDKCheckPage, SerialPage, StudioLogView, TaskConsole, ToolCard, ToolOverlay, TwoLineElidedLabel, WorkbenchPage, log_text, tool_defaults


def application() -> QApplication:
    return QApplication.instance() or QApplication([])


def test_tool_defaults_expand_workspace_placeholders() -> None:
    workspace = {
        "path": "C:/ark_sdk",
        "activeApp": "demo",
        "cubeProjectPath": "C:/cube",
        "configPath": "C:/ark_sdk/app/demo/demo.dts",
        "settings": {"default_probe": "stlink"},
    }
    tool = {"inputs": [
        {"name": "app", "type": "app", "default": None},
        {"name": "probe", "type": "probe", "default": None},
        {"name": "out", "type": "directory", "default": "${workspace_parent}/release"},
    ]}
    assert tool_defaults(tool, workspace) == {"app": "demo", "probe": "stlink", "out": "C:\\release"}


def test_workbench_renders_dynamic_tool_cards() -> None:
    application()
    page = WorkbenchPage()
    page.resize(1200, 700)
    tool = {"id": "dts.check", "title": "检查DTS", "description": "检查配置", "category": "配置", "parameterMode": "none"}
    page.set_tools([tool], {"dts.check": {"available": True, "message": "可用"}})
    assert page.count.text() == "1 项"
    assert page.tool_grid.count() >= 1


def test_workbench_filters_tools_and_switches_to_compact_list() -> None:
    application()
    page = WorkbenchPage()
    tools = [
        {"id": "dts.check", "title": "检查 DTS", "description": "检查配置", "category": "配置", "parameterMode": "none"},
        {"id": "keil.build.full", "title": "全量编译", "description": "编译工程", "category": "构建", "parameterMode": "none"},
    ]
    availability = {"dts.check": {"available": True}, "keil.build.full": {"available": False}}
    page.set_tools(tools, availability)
    page.status_filter.setCurrentIndex(page.status_filter.findData("unavailable"))
    assert page.count.text() == "1 项"
    page._set_view_mode(1)
    assert page.tool_views.currentWidget() is page.tool_list
    assert page.tool_list.rowCount() == 1


def test_serial_page_formats_text_and_hex() -> None:
    application()
    page = SerialPage()
    page.encoding.setCurrentText("UTF-8")
    page.append_serial("rx", {"timestamp": "2026-09-04T12:00:00.000+00:00", "dataHex": "e4 b8 ad"})
    assert "中" in page.output.toPlainText()
    page.encoding.setCurrentText("HEX")
    page.local_echo.setChecked(True)
    page.append_serial("tx", {"timestamp": "", "dataHex": "00 ff"})
    assert "00 FF" in page.output.toPlainText()
    page.encoding.setCurrentText("ASCII")
    page.append_serial("rx", {"timestamp": "", "dataHex": "41 0d 0a 01"})
    assert r"A\r\n" in page.output.toPlainText()
    assert r"\x01" in page.output.toPlainText()


def test_serial_terminal_sends_shell_key_sequences() -> None:
    application()
    page = SerialPage()
    sent: list[str] = []
    page.write_requested.connect(sent.append)
    page.set_snapshot({"opened": True, "port": "LOOPBACK", "baud": 115200})
    QTest.keyClick(page.output, Qt.Key.Key_A)
    QTest.keyClick(page.output, Qt.Key.Key_Up)
    QTest.keyClick(page.output, Qt.Key.Key_C, Qt.KeyboardModifier.ControlModifier)
    QTest.keyClick(page.output, Qt.Key.Key_Return)
    assert sent == ["61", "1b5b41", "03", "0d0a"]


def test_serial_terminal_accepts_ime_and_local_paste() -> None:
    app = application()
    page = SerialPage()
    sent: list[str] = []
    page.write_requested.connect(sent.append)
    page.set_snapshot({"opened": True, "port": "LOOPBACK", "baud": 115200})
    event = QInputMethodEvent()
    event.setCommitString("中")
    app.sendEvent(page.output, event)
    QApplication.clipboard().setText("ark")
    QTest.keyClick(page.output, Qt.Key.Key_V, Qt.KeyboardModifier.ControlModifier | Qt.KeyboardModifier.ShiftModifier)
    assert sent == ["e4b8ad", "61726b"]


def test_log_view_is_compact_selectable_and_structured() -> None:
    application()
    view = LogView()
    view.set_visible_rows(2)
    view.set_logs([{"timestamp": "2026-09-06T12:00:00+00:00", "level": "warning", "source": "probe", "text": "未连接"}])
    assert "WARNING" in view.toPlainText()
    assert "probe" in view.toPlainText()
    assert view.maximumHeight() < 80
    assert view.textInteractionFlags() & Qt.TextInteractionFlag.TextSelectableByMouse


def test_recent_page_accepts_two_log_selections() -> None:
    application()
    page = RecentPage()
    tasks = [
        {"id": "1", "state": "succeeded", "title": "A", "app": "demo", "percent": 100, "createdAt": "1", "logs": [{"source": "a", "text": "left"}]},
        {"id": "2", "state": "failed", "title": "B", "app": "demo", "percent": 30, "createdAt": "2", "logs": [{"source": "b", "text": "right"}]},
    ]
    page.set_tasks(tasks)
    page.table.selectRow(0)
    page.table.selectRow(1)
    page._compare()
    assert "left" in page.left.toPlainText()
    assert "right" in page.right.toPlainText()


def test_artifact_page_requests_text_preview() -> None:
    application()
    page = ArtifactsPage()
    requested: list[str] = []
    page.preview_requested.connect(requested.append)
    page.set_artifacts([{"id": "a", "kind": "MAP", "name": "demo.map", "size": 10, "relativePath": "demo.map", "textPreview": True}])
    page.table.selectRow(0)
    page._selected()
    assert requested[-1] == "a"


def test_tool_details_use_an_in_window_overlay() -> None:
    application()
    host = QWidget()
    host.resize(1080, 680)
    host.show()
    overlay = ToolOverlay(host)
    executed: list[tuple[dict, dict]] = []
    overlay.execute_requested.connect(lambda tool, values: executed.append((tool, values)))
    tool = {
        "id": "project.clone",
        "title": "克隆工程",
        "description": "创建工程副本",
        "parameterMode": "dynamic",
        "resources": ["project"],
        "inputs": [{"name": "name", "label": "名称", "type": "text"}],
        "confirmation": {"summary": "将创建新目录"},
    }
    overlay.show_tool(tool, {"activeApp": "demo"}, {"name": "demo"})
    assert overlay.parentWidget() is host
    assert not overlay.isWindow()
    assert overlay.isVisible()
    assert overlay.panel is not None
    assert overlay.panel.width() >= 720
    assert overlay._animation is not None
    QTest.qWait(220)
    assert overlay.panel.graphicsEffect() is None
    overlay._execute()
    assert not executed
    assert overlay.confirmation is not None
    overlay.confirmation.setChecked(True)
    overlay._execute()
    assert executed[0][1] == {"name": "demo"}


def test_main_search_is_centered_in_the_top_available_area() -> None:
    app = application()
    with patch("studio.app.BrainClient.start"):
        window = MainWindow(Path.cwd())
    window.resize(1440, 900)
    window.show()
    app.processEvents()
    search_center = window.search.mapTo(window, window.search.rect().center()).x()
    assert abs(search_center - window.width() // 2) < 120
    assert window.search.height() == 34
    assert window.search_host.y() == 4
    window._closing = True
    window.close()


def test_page_switch_uses_fade_and_last_navigation_wins() -> None:
    app = application()
    stack = FadeStackedWidget()
    pages = [QWidget(), QWidget(), QWidget()]
    for page in pages:
        stack.addWidget(page)
    stack.resize(600, 400)
    stack.show()
    stack.show_page(pages[1])
    assert not stack._overlay._old_pixmap.isNull()
    assert not stack._overlay._new_pixmap.isNull()
    stack.show_page(pages[2])
    QTest.qWait(420)
    app.processEvents()
    assert stack.currentWidget() is pages[2]
    assert all(page.graphicsEffect() is None for page in pages)
    assert not stack._overlay.isVisible()
    stack.close()


def test_single_filtered_tool_card_keeps_compact_geometry() -> None:
    app = application()
    page = WorkbenchPage()
    page.resize(1200, 700)
    page.show()
    tools = [
        {"id": "one", "title": "字符编码转换", "description": "转换文件编码", "category": "维护", "parameterMode": "dynamic"},
        {"id": "two", "title": "检查DTS", "description": "检查配置", "category": "配置", "parameterMode": "none"},
    ]
    availability = {item["id"]: {"available": True, "message": "可用"} for item in tools}
    page.set_tools(tools, availability, "字符编码")
    app.processEvents()
    card = page.tool_grid.itemAt(0).widget()
    assert card is not None
    assert 164 <= card.height() <= 190
    assert card.width() > 250
    assert page.scroll.horizontalScrollBarPolicy() == Qt.ScrollBarPolicy.ScrollBarAlwaysOff
    description = card.findChild(TwoLineElidedLabel, "toolDescription")
    assert description is not None
    assert description.height() >= description.fontMetrics().lineSpacing() * 2
    page.close()


def test_tool_refresh_never_promotes_old_cards_to_windows() -> None:
    application()
    page = WorkbenchPage()
    page.resize(1200, 700)
    page.show()
    first = {"id": "one", "title": "检查DTS", "description": "检查配置", "category": "配置", "parameterMode": "none"}
    second = {"id": "two", "title": "生成DTS", "description": "生成配置", "category": "配置", "parameterMode": "none"}
    page.set_tools([first], {"one": {"available": True, "message": "可用"}})
    old_card = page.tool_grid.itemAt(0).widget()
    page.set_tools([second], {"two": {"available": True, "message": "可用"}})
    assert old_card is not None
    assert not old_card.isVisible()
    assert not old_card.isWindow()
    page.close()


def test_no_parameter_tool_starts_without_loading_overlay_params() -> None:
    with patch("studio.app.BrainClient.start"):
        window = MainWindow(Path.cwd())
    calls: list[tuple[str, dict]] = []
    window.brain.request = lambda method, params=None, *args, **kwargs: calls.append((method, params or {}))  # type: ignore[method-assign]
    window.execute_tool({"id": "keil.sync", "title": "同步Keil", "parameterMode": "none", "uiMode": "task"})
    assert calls == [("task.start", {"toolId": "keil.sync", "params": {}})]
    window._closing = True
    window.close()


def test_card_body_opens_details_and_only_execute_button_runs() -> None:
    app = application()
    tool = {"id": "dts.check", "title": "检查DTS", "description": "检查当前配置", "category": "配置", "parameterMode": "none"}
    card = ToolCard(tool, True, "可用")
    details: list[dict] = []
    executions: list[dict] = []
    card.detail_requested.connect(details.append)
    card.execute_requested.connect(executions.append)
    card.resize(420, 176)
    card.show()
    QTest.mouseClick(card, Qt.MouseButton.LeftButton, pos=card.rect().center())
    assert details == [tool]
    assert not executions
    QTest.mouseClick(card.execute, Qt.MouseButton.LeftButton)
    assert executions == [tool]
    card.close()


def test_running_task_locks_execution_but_keeps_details_available() -> None:
    application()
    tool = {"id": "dts.check", "title": "检查DTS", "description": "检查当前配置", "category": "配置", "parameterMode": "none"}
    card = ToolCard(tool, True, "可用", execution_locked=True)
    details: list[dict] = []
    card.detail_requested.connect(details.append)
    card.resize(420, 176)
    card.show()
    assert not card.execute.isEnabled()
    QTest.mouseClick(card, Qt.MouseButton.LeftButton, pos=card.rect().center())
    assert details == [tool]
    card.close()


def test_no_parameter_card_details_do_not_start_a_task() -> None:
    app = application()
    with patch("studio.app.BrainClient.start"):
        window = MainWindow(Path.cwd())
    window.show()
    app.processEvents()
    calls: list[tuple[str, dict]] = []
    window.brain.request = lambda method, params=None, *args, **kwargs: calls.append((method, params or {}))  # type: ignore[method-assign]
    window.sdk_check = {"tools": {"keil.sync": {"available": True, "message": "可用"}}}
    window.detail_tool({"id": "keil.sync", "title": "同步Keil", "description": "同步工程", "parameterMode": "none", "uiMode": "task"})
    assert window.overlay.isVisible()
    assert calls == []
    window.overlay.close_overlay()
    QTest.qWait(180)
    window._closing = True
    window.close()


def test_sdk_check_page_uses_compact_columns_and_tooltips() -> None:
    application()
    page = SDKCheckPage()
    message = "E:/a/very/long/path/to/the/current/keil/project/file.uvprojx"
    page.set_snapshot({
        "state": "succeeded", "mode": "deep", "percent": 100,
        "items": [{"status": "ready", "title": "Keil工程", "message": message, "durationMs": 12}],
        "logs": [],
    })
    assert page.items.columnCount() == 3
    assert page.items.item(0, 1).toolTip() == message
    assert page.deep_button.isEnabled()
    assert page.progress.maximumWidth() == 520
    assert page.ring._state == "succeeded"


def test_tool_console_expands_for_workbench_logs() -> None:
    application()
    console = TaskConsole()
    assert console.minimumHeight() >= 220
    assert console.maximumHeight() >= 16_000
    assert console.logs.textInteractionFlags() & Qt.TextInteractionFlag.TextSelectableByMouse


def test_task_console_structures_problems_and_artifacts() -> None:
    application()
    console = TaskConsole()
    console.set_task({
        "id": "1", "toolId": "dts.check", "title": "检查 DTS", "state": "failed", "percent": 100,
        "logs": [{"level": "error", "source": "tool:dts.check", "text": "缺少 compatible"}],
        "artifacts": [{"name": "report.txt", "kind": "REPORT", "relativePath": "build/report.txt"}],
    })
    assert console.tabs.currentIndex() == 1
    assert console.issues.rowCount() == 1
    assert console.artifacts.rowCount() == 1
    assert console.tabs.tabText(1) == "问题 1"


def test_task_console_switches_from_live_output_to_stable_summary() -> None:
    app = application()
    console = TaskConsole()
    console.resize(1000, 300)
    console.show()
    running = {"id": "1", "title": "增量编译", "state": "running", "stage": "build", "percent": 35, "logs": []}
    console.set_task(running)
    app.processEvents()
    assert console.tabs.currentIndex() == 2
    assert console.cancel.isEnabled()
    console.set_task({**running, "state": "succeeded", "percent": 100, "finishedAt": "2026-09-08T12:00:01+00:00"})
    app.processEvents()
    assert console.tabs.currentIndex() == 0
    assert console.cancel.text() == "已完成"
    assert console.cancel.property("taskState") == "succeeded"
    assert not console.cancel.isEnabled()
    assert console.rerun.isVisible()
    assert "任务已完成" in console.summary.text()
    console.close()


def test_studio_log_is_read_only_inside_filterable_drawer() -> None:
    application()
    panel = GlobalLogPanel()
    panel.append_log({"level": "info", "source": "studio", "text": "ready"})
    panel.append_log({"level": "error", "source": "brain", "text": "failed"})
    assert panel.log.isReadOnly()
    assert panel.log.textInteractionFlags() & Qt.TextInteractionFlag.TextSelectableByMouse
    panel.filter.setCurrentIndex(panel.filter.findData("error"))
    assert "failed" in panel.log.toPlainText()
    assert "ready" not in panel.log.toPlainText()


def test_log_updates_do_not_force_horizontal_scroll() -> None:
    app = application()
    log = LogView()
    log.resize(240, 100)
    log.show()
    log.set_logs([{"source": "check", "text": "A" * 300}])
    app.processEvents()
    horizontal = log.horizontalScrollBar()
    horizontal.setValue(max(1, horizontal.maximum() // 3))
    position = horizontal.value()
    log.append_log({"source": "check", "text": "B" * 300})
    assert horizontal.value() == position
    log.set_logs([{"source": "check", "text": "C" * 300}])
    assert horizontal.value() == 0
    log.close()


def test_workbench_tool_log_uses_two_fifths_of_main_area() -> None:
    app = application()
    with patch("studio.app.BrainClient.start"):
        window = MainWindow(Path.cwd())
    window.workspace["opened"] = True
    window.resize(1440, 900)
    window.show()
    window.set_page("workbench")
    app.processEvents()
    sizes = window.main_splitter.sizes()
    assert 0.37 <= sizes[1] / sum(sizes) <= 0.43
    assert window.status_bar.height() == 32
    assert not window.global_log_panel.isVisible()
    window._toggle_global_logs(True)
    assert window.global_log_panel.isVisible()
    page_extent = window.pages.size()
    window.set_page("recent")
    assert window.pages._overlay.isVisible()
    assert not window.pages._overlay._old_pixmap.isNull()
    assert not window.pages._overlay._new_pixmap.isNull()
    assert window.pages.size() == page_extent
    QTest.qWait(260)
    assert not window.pages._overlay.isVisible()
    window._closing = True
    window.close()


def test_primary_pages_share_the_same_content_margins() -> None:
    with patch("studio.app.BrainClient.start"):
        window = MainWindow(Path.cwd())
    margins = []
    for name in ("workbench", "recent", "serial", "artifacts", "sdk-check", "settings"):
        layout = window.page_map[name].layout()
        assert layout is not None
        value = layout.contentsMargins()
        margins.append((value.left(), value.top(), value.right(), value.bottom()))
    assert len(set(margins)) == 1
    assert margins[0] == (28, 24, 28, 24)
    window._closing = True
    window.close()


def test_every_primary_page_uses_the_complete_fade_surface() -> None:
    app = application()
    with patch("studio.app.BrainClient.start"):
        window = MainWindow(Path.cwd())
    window.workspace["opened"] = True
    window.resize(1080, 680)
    window.show()
    for name in ("workbench", "recent", "serial", "artifacts", "sdk-check", "settings"):
        if window.pages.currentWidget() is window.page_map[name]:
            continue
        window.set_page(name)
        assert window.pages._overlay.geometry() == window.pages.rect()
        assert window.pages._overlay.isVisible()
        QTest.qWait(240)
        app.processEvents()
        assert window.pages.currentWidget() is window.page_map[name]
        assert not window.pages._overlay.isVisible()
    window._closing = True
    window.close()


def test_log_source_is_shortened_to_final_tool_name() -> None:
    text = log_text({"timestamp": "", "level": "success", "source": "tool:keil.build.incremental", "text": "完成"})
    assert "tool:" not in text
    assert "incremental" in text


def test_background_errors_do_not_open_modal_warning() -> None:
    with patch("studio.app.BrainClient.start"), patch("studio.app.QMessageBox.warning") as warning:
        window = MainWindow(Path.cwd())
        window._error("工作区检查失败")
        warning.assert_not_called()
        assert "工作区检查失败" in window.studio_log.toPlainText()
        assert not window.global_log_panel.isHidden()
        assert window.status_bar.error.text() == "错误 1"
        window._closing = True
        window.close()
