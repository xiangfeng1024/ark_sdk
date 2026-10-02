# SPDX-FileCopyrightText: Copyright (c) 2026 Ark Crew. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Ark-Crew-NonCommercial-1.0

"""Native Qt workbench for ARK CREW Embedded SDK development."""

from __future__ import annotations

from datetime import datetime
import math
import os
from pathlib import Path
import sys
import time
from typing import Any, Callable

from PySide6.QtCore import QByteArray, QEasingCurve, QFileSystemWatcher, QPointF, Property, QParallelAnimationGroup, QPropertyAnimation, QRectF, Qt, QTimer, QUrl, Signal
from PySide6.QtGui import QAction, QCloseEvent, QColor, QDesktopServices, QFont, QFontDatabase, QFontInfo, QKeyEvent, QKeySequence, QLinearGradient, QPainter, QPainterPath, QPen, QPixmap, QTextCharFormat, QTextCursor
from PySide6.QtWidgets import (
    QAbstractItemView,
    QApplication,
    QCheckBox,
    QComboBox,
    QFileDialog,
    QFormLayout,
    QFrame,
    QGraphicsOpacityEffect,
    QGridLayout,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMenu,
    QMessageBox,
    QPlainTextEdit,
    QProgressBar,
    QProxyStyle,
    QPushButton,
    QScrollArea,
    QSizePolicy,
    QSpinBox,
    QSplitter,
    QStackedWidget,
    QSystemTrayIcon,
    QStyle,
    QTabWidget,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from . import __version__
from .brain_client import BrainClient
from .icons import ASSET_ROOT, app_icon, icon, logo_pixmap
from .theme import STYLESHEET
from .ui_state import UiState


TERMINAL_STATES = {"succeeded", "failed", "cancelled", "blocked"}
PAGE_MARGINS = (28, 24, 28, 24)
PAGE_SPACING = 16
STATE_TEXT = {
    "queued": "排队",
    "running": "运行中",
    "succeeded": "成功",
    "failed": "失败",
    "cancelled": "已取消",
    "blocked": "已阻止",
}

TOOL_ACTION_TEXT = {
    "dts.check": "检查 DTS",
    "dts.generate": "生成 DTS",
    "keil.sync": "同步 Keil",
    "keil.build.incremental": "开始编译",
    "keil.build.full": "开始编译",
    "keil.flash.incremental": "编译并烧录",
    "keil.flash.only": "开始烧录",
    "keil.clean": "开始清理",
    "project.create_app": "新建 App",
    "project.clone": "开始克隆",
    "project.components": "打开配置",
    "project.package": "开始打包",
    "analysis.memory": "分析内存",
    "sdk.encoding.convert": "转换编码",
    "test.demo_progress": "运行测试",
}


def tool_action_text(tool: dict[str, Any], state: str = "") -> str:
    if state in {"queued", "running"}:
        return "运行中..."
    if state == "failed":
        return "重试"
    if state == "succeeded":
        return "再次运行"
    return TOOL_ACTION_TEXT.get(str(tool.get("id", "")), "运行工具")


def task_duration_ms(task: dict[str, Any]) -> int:
    def parse(value: Any) -> datetime | None:
        if not value:
            return None
        try:
            return datetime.fromisoformat(str(value).replace("Z", "+00:00"))
        except ValueError:
            return None

    started = parse(task.get("startedAt") or task.get("createdAt"))
    finished = parse(task.get("finishedAt")) or (datetime.now().astimezone() if started and task.get("state") in {"queued", "running"} else None)
    return max(0, round((finished - started).total_seconds() * 1000)) if started and finished else 0


def format_duration(milliseconds: int) -> str:
    return f"{milliseconds / 1000:.1f} 秒" if milliseconds >= 1000 else f"{milliseconds} 毫秒"


def page_heading(title: str, description: str, action: QWidget | None = None) -> QWidget:
    widget = QWidget()
    layout = QHBoxLayout(widget)
    layout.setContentsMargins(0, 0, 0, 4)
    text = QVBoxLayout()
    text.setSpacing(1)
    heading = QLabel(title)
    heading.setObjectName("pageTitle")
    copy = QLabel(description)
    copy.setObjectName("pageDescription")
    copy.setWordWrap(True)
    text.addWidget(heading)
    text.addWidget(copy)
    layout.addLayout(text, 1)
    if action is not None:
        layout.addWidget(action, 0, Qt.AlignmentFlag.AlignTop)
    return widget


def configure_page_layout(layout: QVBoxLayout) -> None:
    """Keep every primary page on one shared content rectangle."""
    layout.setContentsMargins(*PAGE_MARGINS)
    layout.setSpacing(PAGE_SPACING)


def primary_button(text: str, icon_name: str = "play") -> QPushButton:
    button = QPushButton(icon(icon_name, "#04111d", 16), text)
    button.setProperty("primary", True)
    return button


def status_label(text: str, status: str) -> QLabel:
    label = QLabel(text)
    label.setProperty("status", status)
    return label


def safe_text(value: Any) -> str:
    return str(value).encode("utf-8", errors="replace").decode("utf-8")


def short_log_source(value: Any) -> str:
    source = safe_text(value or "brain")
    if source.startswith("tool:"):
        source = source.rsplit(".", 1)[-1].split(":", 1)[-1]
    elif source == "sdk_check":
        source = "check"
    return source[:12]


def human_size(value: int) -> str:
    size = float(value)
    for unit in ("B", "KB", "MB", "GB"):
        if size < 1024 or unit == "GB":
            return f"{size:.0f} {unit}" if unit == "B" else f"{size:.1f} {unit}"
        size /= 1024
    return f"{value} B"


def log_text(entry: dict[str, Any], timestamps: bool = True) -> str:
    timestamp = str(entry.get("timestamp", ""))
    time_text = ""
    if timestamps and timestamp:
        try:
            time_text = datetime.fromisoformat(timestamp.replace("Z", "+00:00")).strftime("%H:%M:%S") + "  "
        except ValueError:
            time_text = timestamp + "  "
    level = safe_text(entry.get("level", "info")).upper()
    source = short_log_source(entry.get("source", "brain"))
    message = safe_text(entry.get("text", "")).rstrip()
    return f"{time_text}{level:<7} {source:<12} {message}"


class LogView(QPlainTextEdit):
    LEVEL_COLORS = {
        "debug": "#71889a",
        "info": "#8fcaf0",
        "success": "#42e0ae",
        "warning": "#ffd166",
        "error": "#ff718b",
    }

    def __init__(self) -> None:
        super().__init__()
        self.setReadOnly(True)
        self.setUndoRedoEnabled(False)
        self.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)
        self.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByKeyboard | Qt.TextInteractionFlag.TextSelectableByMouse)
        font = QFont()
        font.setFamilies(["Cascadia Mono", "Microsoft YaHei UI", "Noto Sans SC"])
        font.setPointSizeF(9.5)
        font.setHintingPreference(QFont.HintingPreference.PreferFullHinting)
        font.setStyleStrategy(QFont.StyleStrategy.PreferAntialias)
        self.setFont(font)
        self.timestamps = True

    def set_logs(self, logs: list[dict[str, Any]], timestamps: bool = True) -> None:
        self.timestamps = timestamps
        self.clear()
        for entry in logs:
            self._append_entry(entry)
        self.verticalScrollBar().setValue(self.verticalScrollBar().maximum())
        self.horizontalScrollBar().setValue(0)

    def append_log(self, entry: dict[str, Any]) -> None:
        bar = self.verticalScrollBar()
        horizontal = self.horizontalScrollBar()
        horizontal_position = horizontal.value()
        follow = bar.maximum() - bar.value() - bar.pageStep() <= 24
        self._append_entry(entry)
        horizontal.setValue(horizontal_position)
        if follow:
            bar.setValue(bar.maximum())

    def set_visible_rows(self, rows: int) -> None:
        margins = self.contentsMargins()
        height = self.fontMetrics().lineSpacing() * max(rows, 1) + margins.top() + margins.bottom() + 18
        self.setMinimumHeight(height)
        self.setMaximumHeight(height)

    def _append_entry(self, entry: dict[str, Any]) -> None:
        cursor = self.textCursor()
        cursor.movePosition(QTextCursor.MoveOperation.End)
        if not self.document().isEmpty():
            cursor.insertBlock()
        level = str(entry.get("level", "info")).lower()
        line_format = QTextCharFormat()
        line_format.setForeground(QColor(self.LEVEL_COLORS.get(level, "#b8c9d6")))
        cursor.insertText(log_text(entry, self.timestamps), line_format)
        self.setTextCursor(cursor)


class ElidedLabel(QLabel):
    def __init__(self, text: str = "") -> None:
        super().__init__()
        self._full_text = text
        self.setMinimumWidth(0)
        self.setSizePolicy(QSizePolicy.Policy.Ignored, QSizePolicy.Policy.Preferred)
        self.setToolTip(text)
        self._update_text()

    def set_full_text(self, text: str) -> None:
        self._full_text = text
        self.setToolTip(text)
        self._update_text()

    def resizeEvent(self, event: Any) -> None:
        super().resizeEvent(event)
        self._update_text()

    def _update_text(self) -> None:
        width = max(self.width(), 40)
        QLabel.setText(self, self.fontMetrics().elidedText(self._full_text, Qt.TextElideMode.ElideRight, width))


class TwoLineElidedLabel(QLabel):
    """Wrap plain text into at most two stable, elided display lines."""

    def __init__(self, text: str = "") -> None:
        super().__init__()
        self._full_text = text
        self.setMinimumWidth(0)
        self.setSizePolicy(QSizePolicy.Policy.Ignored, QSizePolicy.Policy.Fixed)
        self.setToolTip(text)
        self._update_text()

    def resizeEvent(self, event: Any) -> None:
        super().resizeEvent(event)
        self._update_text()

    def _update_text(self) -> None:
        metrics = self.fontMetrics()
        width = max(self.width(), 80)
        first = ""
        remainder = self._full_text.strip()
        while remainder:
            candidate = first + remainder[0]
            if first and metrics.horizontalAdvance(candidate) > width:
                break
            first = candidate
            remainder = remainder[1:]
        second = metrics.elidedText(remainder, Qt.TextElideMode.ElideRight, width) if remainder else ""
        QLabel.setText(self, first + ("\n" + second if second else ""))
        self.setFixedHeight(metrics.lineSpacing() * 2 + 4)


class PageTransitionOverlay(QWidget):
    def __init__(self, parent: QWidget) -> None:
        super().__init__(parent)
        self._progress = 0.0
        self._old_pixmap = QPixmap()
        self._new_pixmap = QPixmap()
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents, True)
        self.hide()

    def set_snapshots(self, old_pixmap: QPixmap, new_pixmap: QPixmap) -> None:
        self._old_pixmap = old_pixmap
        self._new_pixmap = new_pixmap
        self._progress = 0.0
        self.update()

    def get_progress(self) -> float:
        return self._progress

    def set_progress(self, value: float) -> None:
        self._progress = value
        self.update()

    progress = Property(float, get_progress, set_progress)

    def paintEvent(self, _event: Any) -> None:
        if self._old_pixmap.isNull() or self._new_pixmap.isNull():
            return
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor("#07101f"))
        painter.setOpacity(1.0 - self._progress)
        painter.drawPixmap(self.rect(), self._old_pixmap)
        painter.setOpacity(self._progress)
        painter.drawPixmap(self.rect(), self._new_pixmap)
        painter.end()


class FadeStackedWidget(QStackedWidget):
    """Cross-fade complete page snapshots without native child-window artifacts."""

    def __init__(self) -> None:
        super().__init__()
        self._animation: QPropertyAnimation | None = None
        self._overlay = PageTransitionOverlay(self)

    def show_page(self, page: QWidget, animated: bool = True) -> None:
        if page is self.currentWidget():
            return
        current = self.currentWidget()
        old_snapshot = self._render_page(current)
        if self._animation is not None:
            self._animation.stop()
        self._overlay.hide()
        QStackedWidget.setCurrentWidget(self, page)
        if page.layout() is not None:
            page.layout().activate()
        new_snapshot = self._render_page(page)
        if not animated or old_snapshot.isNull() or new_snapshot.isNull() or not self.isVisible():
            return
        self._overlay.setGeometry(self.rect())
        self._overlay.set_snapshots(old_snapshot, new_snapshot)
        self._overlay.show()
        self._overlay.raise_()
        animation = QPropertyAnimation(self._overlay, b"progress", self)
        animation.setDuration(220)
        animation.setStartValue(0.0)
        animation.setEndValue(1.0)
        animation.setEasingCurve(QEasingCurve.Type.InOutCubic)
        animation.finished.connect(self._overlay.hide)
        self._animation = animation
        animation.start()

    def _render_page(self, page: QWidget | None) -> QPixmap:
        if page is None or not self.isVisible() or self.width() <= 0 or self.height() <= 0:
            return QPixmap()
        page.setGeometry(0, 0, self.width(), self.height())
        page.ensurePolished()
        if page.layout() is not None:
            page.layout().activate()
        pixmap = QPixmap(self.size())
        pixmap.fill(QColor("#081320"))
        page.render(pixmap)
        return pixmap

    def resizeEvent(self, event: Any) -> None:
        super().resizeEvent(event)
        self._overlay.setGeometry(self.rect())


class ArkStyle(QProxyStyle):
    """Draw checkbox state as an outline plus check mark, never a solid tile."""

    def drawPrimitive(self, element: QStyle.PrimitiveElement, option: Any, painter: QPainter, widget: QWidget | None = None) -> None:
        if element != QStyle.PrimitiveElement.PE_IndicatorCheckBox:
            super().drawPrimitive(element, option, painter, widget)
            return
        rect = option.rect.adjusted(1, 1, -1, -1)
        checked = bool(option.state & QStyle.StateFlag.State_On)
        hovered = bool(option.state & QStyle.StateFlag.State_MouseOver)
        enabled = bool(option.state & QStyle.StateFlag.State_Enabled)
        painter.save()
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.setBrush(QColor("#071525"))
        painter.setPen(QPen(QColor("#4a86aa" if hovered else "#315a78" if enabled else "#20384b"), 1.2))
        painter.drawRoundedRect(rect, 4, 4)
        if checked:
            path = QPainterPath()
            path.moveTo(rect.left() + rect.width() * 0.22, rect.center().y())
            path.lineTo(rect.left() + rect.width() * 0.43, rect.bottom() - rect.height() * 0.24)
            path.lineTo(rect.right() - rect.width() * 0.18, rect.top() + rect.height() * 0.25)
            painter.setPen(QPen(QColor("#51c9ef" if enabled else "#52798b"), 2.0, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap, Qt.PenJoinStyle.RoundJoin))
            painter.drawPath(path)
        painter.restore()


class EnergyRing(QWidget):
    """Compact task activity indicator inspired by the original Studio ring."""

    def __init__(self) -> None:
        super().__init__()
        self.setFixedSize(36, 36)
        self._active = False
        self._state = "idle"
        self._started = time.monotonic()
        self._timer = QTimer(self)
        self._timer.setInterval(40)
        self._timer.timeout.connect(self.update)

    def set_state(self, state: str) -> None:
        self._state = state
        active = state in {"queued", "running"}
        if active and not self._active:
            self._started = time.monotonic()
            self._timer.start()
        elif not active:
            self._timer.stop()
        self._active = active
        self.update()

    def paintEvent(self, _event: Any) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        center = QPointF(self.width() / 2, self.height() / 2)
        elapsed = time.monotonic() - self._started
        symbols = {"succeeded": ("✓", "#38d39f"), "failed": ("×", "#ff6b7a"), "blocked": ("×", "#ff6b7a"), "cancelled": ("■", "#8298aa")}
        if self._state in symbols:
            symbol, color = symbols[self._state]
            painter.setPen(QColor(color))
            font = painter.font()
            font.setPointSizeF(13.0)
            font.setBold(True)
            painter.setFont(font)
            painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, symbol)
            painter.end()
            return
        breathe = 0.45 + 0.35 * (1 + math.sin(elapsed * math.tau / 3.6)) / 2 if self._active else 0.28
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(QColor(87, 112, 255, round(70 * breathe)))
        painter.drawEllipse(center, 10 + breathe * 3, 10 + breathe * 3)
        for ring, radius, count, direction, period in ((0, 12.5, 10, 1, 9.0), (1, 8.5, 6, -1, 12.0)):
            rotation = direction * elapsed * math.tau / period if self._active else 0.0
            for index in range(count):
                angle = rotation + index * math.tau / count
                pulse = 0.45 + 0.55 * (1 + math.sin(elapsed * math.tau / 2.8 - index * 0.7)) / 2 if self._active else 0.55
                color = QColor("#8b5cff" if ring else "#3db8ff")
                color.setAlpha(round(80 + 175 * pulse))
                painter.setBrush(color)
                point = QPointF(center.x() + math.cos(angle) * radius, center.y() + math.sin(angle) * radius)
                painter.drawEllipse(point, 1.4 if ring else 1.7, 1.4 if ring else 1.7)
        painter.end()


class AnimatedProgressBar(QProgressBar):
    def __init__(self) -> None:
        super().__init__()
        self._offset = 0
        self._active = False
        self._timer = QTimer(self)
        self._timer.timeout.connect(self._advance)

    def set_active(self, active: bool) -> None:
        self._active = active
        if active:
            self._timer.start(70)
        else:
            self._timer.stop()
        self.update()

    def _advance(self) -> None:
        self._offset = (self._offset + 7) % max(self.width(), 1)
        self.update()

    def paintEvent(self, _event: Any) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        bounds = self.rect().adjusted(1, 1, -1, -1)
        painter.setPen(QPen(QColor("#313f75"), 1))
        painter.setBrush(QColor("#050718"))
        painter.drawRoundedRect(bounds, 7, 7)
        ratio = 0.0 if self.maximum() <= self.minimum() else (self.value() - self.minimum()) / (self.maximum() - self.minimum())
        fill_width = max(0, int(bounds.width() * max(0.0, min(1.0, ratio))))
        if fill_width:
            fill = bounds.adjusted(0, 0, -(bounds.width() - fill_width), 0)
            gradient = QLinearGradient(fill.left(), 0, fill.right(), 0)
            gradient.setColorAt(0.0, QColor("#258cff"))
            gradient.setColorAt(0.54, QColor("#6579ff"))
            gradient.setColorAt(1.0, QColor("#a956ff"))
            painter.setPen(Qt.PenStyle.NoPen)
            painter.setBrush(gradient)
            painter.drawRoundedRect(fill, 7, 7)
            if self._active:
                highlight = QLinearGradient(self._offset - 60, 0, self._offset + 60, 0)
                highlight.setColorAt(0.0, QColor(255, 255, 255, 0))
                highlight.setColorAt(0.5, QColor(255, 255, 255, 145))
                highlight.setColorAt(1.0, QColor(255, 255, 255, 0))
                painter.setBrush(highlight)
                painter.setClipRect(fill)
                painter.drawRoundedRect(fill, 7, 7)
        painter.end()


class ToolCard(QFrame):
    execute_requested = Signal(object)
    detail_requested = Signal(object)

    def __init__(
        self,
        tool: dict[str, Any],
        available: bool,
        reason: str,
        execution_locked: bool = False,
        recent_task: dict[str, Any] | None = None,
    ) -> None:
        super().__init__()
        self.tool = tool
        self.available = available
        self.setObjectName("toolCard")
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.execution_locked = execution_locked
        self.recent_task = recent_task or {}
        self.setFixedHeight(176)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(16, 14, 16, 14)
        layout.setSpacing(8)
        top = QHBoxLayout()
        category = QLabel(str(tool.get("category", "工具")))
        category.setObjectName("tag")
        category.setSizePolicy(QSizePolicy.Policy.Maximum, QSizePolicy.Policy.Fixed)
        mode = QLabel({"none": "无需参数", "fixed": "固定参数", "dynamic": "本次参数"}.get(str(tool.get("parameterMode")), "工具"))
        mode.setObjectName("tag")
        mode.setSizePolicy(QSizePolicy.Policy.Maximum, QSizePolicy.Policy.Fixed)
        top.addWidget(category)
        top.addStretch()
        top.addWidget(mode)
        layout.addLayout(top)
        title = ElidedLabel(str(tool.get("title", tool.get("id", "工具"))))
        title.setObjectName("toolTitle")
        description = TwoLineElidedLabel(str(tool.get("description", "")))
        description.setObjectName("toolDescription")
        layout.addWidget(title)
        layout.addWidget(description)
        bottom = QHBoxLayout()
        latest_state = str(self.recent_task.get("state", ""))
        availability = QLabel("就绪" if available else "不可用")
        availability.setObjectName("availabilityBadge")
        availability.setProperty("status", "success" if available else "warning")
        availability.setToolTip(reason)
        recent = QLabel(
            f"最近：{STATE_TEXT.get(latest_state, '尚未运行')}"
            + (f" · {str(self.recent_task.get('finishedAt', ''))[11:16]}" if self.recent_task.get("finishedAt") else "")
        )
        recent.setObjectName("muted")
        execute = primary_button(tool_action_text(tool, latest_state), "play")
        execute.setFixedHeight(38)
        execute.setEnabled(available and not execution_locked)
        execute.setToolTip("已有工具正在运行，可点击卡片查看参数" if execution_locked else reason if not available else f"执行{tool.get('title', '')}")
        execute.clicked.connect(lambda: self.execute_requested.emit(self.tool))
        self.execute = execute
        self.availability = availability
        bottom.addWidget(availability)
        bottom.addWidget(recent)
        bottom.addStretch()
        bottom.addWidget(execute)
        layout.addLayout(bottom)

    def mouseReleaseEvent(self, event: Any) -> None:
        if event.button() == Qt.MouseButton.LeftButton:
            self.detail_requested.emit(self.tool)
        super().mouseReleaseEvent(event)


class ToolOverlay(QWidget):
    execute_requested = Signal(object, object)
    closed = Signal()

    def __init__(self, parent: QWidget) -> None:
        super().__init__(parent)
        self.setObjectName("toolOverlay")
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.tool: dict[str, Any] = {}
        self.workspace: dict[str, Any] = {}
        self.widgets: dict[str, QWidget] = {}
        self.panel: QFrame | None = None
        self.confirmation: QCheckBox | None = None
        self.execute: QPushButton | None = None
        self.lock_notice: QLabel | None = None
        self._available = True
        self._execution_locked = False
        self._background = QPixmap()
        self._reveal = 0.0
        self._animation: QParallelAnimationGroup | None = None
        self._panel_effect: QGraphicsOpacityEffect | None = None
        self.hide()

    def show_tool(
        self,
        tool: dict[str, Any],
        workspace: dict[str, Any],
        values: dict[str, Any],
        available: bool = True,
        reason: str = "",
        execution_locked: bool = False,
    ) -> None:
        if self._animation is not None:
            self._animation.stop()
        self.hide()
        self.setGeometry(self.parentWidget().rect())
        snapshot = self.parentWidget().grab()
        if not snapshot.isNull():
            small = snapshot.scaled(max(1, snapshot.width() // 18), max(1, snapshot.height() // 18), Qt.AspectRatioMode.IgnoreAspectRatio, Qt.TransformationMode.SmoothTransformation)
            self._background = small.scaled(snapshot.size(), Qt.AspectRatioMode.IgnoreAspectRatio, Qt.TransformationMode.SmoothTransformation)
        self.tool = tool
        self.workspace = workspace
        self.widgets = {}
        self._available = available
        self._execution_locked = execution_locked
        if self.layout() is not None:
            QWidget().setLayout(self.layout())
        outer = QVBoxLayout(self)
        outer.setContentsMargins(40, 24, 40, 24)
        outer.addStretch()
        row = QHBoxLayout()
        row.addStretch()
        self.panel = QFrame()
        self.panel.setObjectName("overlayPanel")
        panel_width = min(940, max(720, self.width() - 120))
        content_height = 460 + min(len(tool.get("inputs", [])), 4) * 48 + (64 if tool.get("confirmation") else 0)
        panel_height = min(620, max(460, content_height), max(460, self.height() - 48))
        self.panel.setFixedWidth(panel_width)
        self.panel.setFixedHeight(panel_height)
        panel_layout = QVBoxLayout(self.panel)
        panel_layout.setContentsMargins(32, 28, 32, 28)
        panel_layout.setSpacing(16)
        header = QHBoxLayout()
        title = QLabel(str(tool.get("title", "工具")))
        title.setObjectName("overlayTitle")
        close = QPushButton(icon("close", "#a9c3d4", 17), "")
        close.setObjectName("iconButton")
        close.setToolTip("关闭")
        close.setAccessibleName("关闭工具详情")
        close.clicked.connect(self.close_overlay)
        header.addWidget(title, 1)
        header.addWidget(close)
        panel_layout.addLayout(header)
        description = QLabel(str(tool.get("description", "")))
        description.setObjectName("pageDescription")
        description.setWordWrap(True)
        panel_layout.addWidget(description)
        facts = QLabel(
            f"工具 ID：{tool.get('id', '')}\n"
            f"参数模式：{tool.get('parameterMode', 'none')}    资源锁：{', '.join(tool.get('resources', [])) or '无'}"
        )
        facts.setObjectName("mono")
        facts.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        panel_layout.addWidget(facts)
        form = QFormLayout()
        form.setHorizontalSpacing(18)
        form.setVerticalSpacing(10)
        for field in tool.get("inputs", []):
            name = str(field.get("name"))
            widget = self._field_widget(field, values.get(name, field.get("default")))
            widget.setToolTip(str(field.get("description", "")))
            self.widgets[name] = widget
            form.addRow(str(field.get("label", name)), widget)
        if not self.widgets:
            empty = QLabel("此工具无需配置参数，将使用当前工作区和活动 App。")
            empty.setObjectName("muted")
            form.addRow(empty)
        panel_layout.addLayout(form)
        self.lock_notice = QLabel("")
        self.lock_notice.setObjectName("overlayNotice")
        self.lock_notice.setWordWrap(True)
        panel_layout.addWidget(self.lock_notice)
        confirmation = tool.get("confirmation")
        self.confirmation = None
        if confirmation:
            warning = QLabel(str(confirmation.get("summary", "执行前请确认影响范围")))
            warning.setWordWrap(True)
            warning.setProperty("status", "warning")
            panel_layout.addWidget(warning)
            self.confirmation = QCheckBox("我已确认本次操作的目标和影响范围")
            panel_layout.addWidget(self.confirmation)
        panel_layout.addStretch(1)
        buttons = QHBoxLayout()
        buttons.addStretch()
        cancel = QPushButton("取消")
        cancel.clicked.connect(self.close_overlay)
        self.execute = primary_button("执行任务", "play")
        self.execute.clicked.connect(self._execute)
        buttons.addWidget(cancel)
        buttons.addWidget(self.execute)
        panel_layout.addLayout(buttons)
        row.addWidget(self.panel)
        row.addStretch()
        outer.addLayout(row)
        outer.addStretch()
        self._update_execution_state(reason)
        outer.activate()
        self.show()
        self.raise_()
        self.setFocus()
        self._start_animation(True)

    def set_execution_locked(self, locked: bool) -> None:
        self._execution_locked = locked
        if self.tool:
            self._update_execution_state("")

    def _update_execution_state(self, reason: str) -> None:
        enabled = self._available and not self._execution_locked
        if self.execute is not None:
            self.execute.setEnabled(enabled)
        if self.lock_notice is None:
            return
        if self._execution_locked:
            self.lock_notice.setText("已有工具正在运行。你仍可查看和调整参数，当前任务结束后才能执行。")
            self.lock_notice.setProperty("status", "warning")
            self.lock_notice.show()
        elif not self._available:
            self.lock_notice.setText(reason or "当前环境不满足该工具的执行条件。")
            self.lock_notice.setProperty("status", "warning")
            self.lock_notice.show()
        else:
            self.lock_notice.hide()
        self.lock_notice.style().unpolish(self.lock_notice)
        self.lock_notice.style().polish(self.lock_notice)

    def get_reveal(self) -> float:
        return self._reveal

    def set_reveal(self, value: float) -> None:
        self._reveal = value
        self.update()

    reveal = Property(float, get_reveal, set_reveal)

    def paintEvent(self, _event: Any) -> None:
        painter = QPainter(self)
        painter.setOpacity(self._reveal)
        if not self._background.isNull():
            painter.drawPixmap(self.rect(), self._background)
        painter.fillRect(self.rect(), QColor(3, 8, 16, 176))
        painter.end()

    def _start_animation(self, opening: bool) -> None:
        if self.panel is None:
            return
        if self._animation is not None:
            self._animation.stop()
        group = QParallelAnimationGroup(self)
        fade = QPropertyAnimation(self, b"reveal", group)
        fade.setDuration(180 if opening else 140)
        fade.setStartValue(0.0 if opening else self._reveal)
        fade.setEndValue(1.0 if opening else 0.0)
        fade.setEasingCurve(QEasingCurve.Type.OutCubic if opening else QEasingCurve.Type.InCubic)
        self._panel_effect = QGraphicsOpacityEffect(self.panel)
        self.panel.setGraphicsEffect(self._panel_effect)
        panel_fade = QPropertyAnimation(self._panel_effect, b"opacity", group)
        panel_fade.setDuration(180 if opening else 140)
        panel_fade.setStartValue(0.0 if opening else 1.0)
        panel_fade.setEndValue(1.0 if opening else 0.0)
        panel_fade.setEasingCurve(QEasingCurve.Type.OutCubic if opening else QEasingCurve.Type.InCubic)
        if not opening:
            group.finished.connect(self._finish_close)
        else:
            group.finished.connect(self._finish_open)
        self._animation = group
        group.start()

    def _finish_open(self) -> None:
        if self.panel is not None:
            self.panel.setGraphicsEffect(None)
        self._panel_effect = None

    def _field_widget(self, field: dict[str, Any], value: Any) -> QWidget:
        field_type = str(field.get("type", "text"))
        if field_type == "boolean":
            widget = QCheckBox()
            widget.setChecked(bool(value))
            return widget
        if field_type == "integer":
            widget = QSpinBox()
            widget.setRange(-2_147_483_648, 2_147_483_647)
            widget.setValue(int(value or 0))
            return widget
        if field_type in {"enum", "app", "probe", "serial_port"}:
            widget = QComboBox()
            options = list(field.get("options", []))
            if field_type == "app":
                options = list(self.workspace.get("apps", []))
            elif field_type == "probe":
                options = ["dap", "stlink"]
            elif field_type == "serial_port":
                options = serial_ports()
            for option in options:
                widget.addItem(str(option), option)
            index = widget.findText(str(value or ""))
            if index >= 0:
                widget.setCurrentIndex(index)
            return widget
        line = QLineEdit(str(value or ""))
        line.setCursorPosition(0)
        line.setPlaceholderText(str(field.get("placeholder", "")))
        if field_type in {"file", "directory"}:
            container = QWidget()
            layout = QHBoxLayout(container)
            layout.setContentsMargins(0, 0, 0, 0)
            browse = QPushButton(icon("folder", "#a9c8d9", 16), "选择")

            def choose() -> None:
                selected = QFileDialog.getExistingDirectory(self, "选择文件夹", line.text()) if field_type == "directory" else QFileDialog.getOpenFileName(self, "选择文件", line.text())[0]
                if selected:
                    line.setText(selected)

            browse.clicked.connect(choose)
            layout.addWidget(line, 1)
            layout.addWidget(browse)
            container.setProperty("lineEdit", line)
            return container
        return line

    def values(self) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for field in self.tool.get("inputs", []):
            name = str(field.get("name"))
            widget = self.widgets[name]
            if isinstance(widget, QCheckBox):
                result[name] = widget.isChecked()
            elif isinstance(widget, QSpinBox):
                result[name] = widget.value()
            elif isinstance(widget, QComboBox):
                result[name] = widget.currentData() if widget.currentData() is not None else widget.currentText()
            elif isinstance(widget, QLineEdit):
                result[name] = widget.text()
            else:
                line = widget.findChild(QLineEdit)
                result[name] = line.text() if line else ""
        return result

    def _execute(self) -> None:
        if self.execute is None or not self.execute.isEnabled():
            return
        if self.confirmation is not None and not self.confirmation.isChecked():
            self.confirmation.setProperty("status", "warning")
            self.confirmation.setText("请先确认本次操作的目标和影响范围")
            return
        self.execute_requested.emit(self.tool, self.values())
        self.close_overlay()

    def close_overlay(self) -> None:
        if self.isVisible():
            self._start_animation(False)

    def _finish_close(self) -> None:
        if self.panel is not None:
            self.panel.setGraphicsEffect(None)
        self._panel_effect = None
        self.hide()
        self.closed.emit()

    def keyPressEvent(self, event: Any) -> None:
        if event.key() == Qt.Key.Key_Escape:
            self.close_overlay()
            event.accept()
            return
        super().keyPressEvent(event)


def serial_ports() -> list[str]:
    if os.environ.get("ARK_STUDIO_TEST_MODE") == "1":
        return ["LOOPBACK"]
    try:
        from serial.tools import list_ports

        return [item.device for item in list_ports.comports()]
    except ImportError:
        return []


class WorkbenchPage(QWidget):
    app_changed = Signal(str)
    execute_tool = Signal(object)
    detail_tool = Signal(object)
    sdk_check_requested = Signal()
    open_project_requested = Signal()

    def __init__(self) -> None:
        super().__init__()
        page_layout = QVBoxLayout(self)
        configure_page_layout(page_layout)
        self.splitter = QSplitter(Qt.Orientation.Vertical)
        tools_area = QWidget()
        root = QVBoxLayout(tools_area)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(16)
        check = QPushButton(icon("history", "#a8c6d8", 16), "SDK 环境检查")
        check.clicked.connect(self.sdk_check_requested)
        explorer = QPushButton(icon("folder", "#a8c6d8", 17), "")
        explorer.setObjectName("iconButton")
        explorer.setToolTip("在资源管理器中打开当前工程")
        explorer.setAccessibleName("打开工程目录")
        explorer.clicked.connect(self.open_project_requested)
        actions = QWidget()
        action_layout = QHBoxLayout(actions)
        action_layout.setContentsMargins(0, 0, 0, 0)
        action_layout.addWidget(explorer)
        action_layout.addWidget(check)
        header = QWidget()
        header_layout = QHBoxLayout(header)
        header_layout.setContentsMargins(0, 0, 0, 0)
        header_layout.setSpacing(24)
        heading = QVBoxLayout()
        heading.setSpacing(4)
        title = QLabel("工程控制台")
        title.setObjectName("pageTitle")
        description = ElidedLabel("集中完成配置、同步、编译、烧录和结果校验。")
        description.setObjectName("pageDescription")
        heading.addWidget(title)
        heading.addWidget(description)
        header_layout.addLayout(heading, 1)
        self.app_combo = QComboBox()
        self.app_combo.currentTextChanged.connect(self.app_changed)
        self.app_combo.setMinimumWidth(220)
        self.app_combo.setMaximumWidth(300)
        self.sdk = status_label("等待检查", "warning")
        for label_text, widget in (("当前工具 App", self.app_combo), ("SDK 状态", self.sdk)):
            block = QVBoxLayout()
            block.setSpacing(4)
            label = QLabel(label_text)
            label.setObjectName("muted")
            block.addWidget(label)
            block.addWidget(widget)
            header_layout.addLayout(block)
        header_layout.addWidget(actions)
        root.addWidget(header)
        section = QHBoxLayout()
        title = QLabel("工具中心")
        title.setObjectName("sectionTitle")
        self.count = QLabel("0 项")
        self.count.setObjectName("muted")
        section.addWidget(title)
        section.addStretch()
        section.addWidget(self.count)
        root.addLayout(section)
        filters = QFrame()
        filters.setObjectName("toolFilters")
        filter_layout = QHBoxLayout(filters)
        filter_layout.setContentsMargins(10, 8, 10, 8)
        filter_layout.setSpacing(8)
        self.category_filter = QComboBox()
        self.category_filter.addItem("全部类型", "")
        self.status_filter = QComboBox()
        self.status_filter.addItem("全部状态", "")
        self.status_filter.addItem("可运行", "available")
        self.status_filter.addItem("不可运行", "unavailable")
        self.status_filter.addItem("最近失败", "recent_failed")
        self.sort_filter = QComboBox()
        self.sort_filter.addItem("推荐顺序", "default")
        self.sort_filter.addItem("最近运行", "recent")
        self.card_view_button = QPushButton(icon("dashboard", "#a9c8d9", 15), "卡片")
        self.list_view_button = QPushButton(icon("list", "#a9c8d9", 15), "列表")
        self.card_view_button.setProperty("segmented", True)
        self.list_view_button.setProperty("segmented", True)
        self.card_view_button.setProperty("active", True)
        for combo in (self.category_filter, self.status_filter, self.sort_filter):
            combo.currentIndexChanged.connect(self._render)
            filter_layout.addWidget(combo)
        filter_layout.addStretch(1)
        filter_layout.addWidget(self.card_view_button)
        filter_layout.addWidget(self.list_view_button)
        root.addWidget(filters)
        self.scroll = QScrollArea()
        self.scroll.setWidgetResizable(True)
        self.scroll.setFrameShape(QFrame.Shape.NoFrame)
        self.scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.tool_widget = QWidget()
        self.tool_grid = QGridLayout(self.tool_widget)
        self.tool_grid.setContentsMargins(0, 0, 0, 0)
        self.tool_grid.setHorizontalSpacing(16)
        self.tool_grid.setVerticalSpacing(16)
        self.tool_grid.setAlignment(Qt.AlignmentFlag.AlignTop)
        self.scroll.setWidget(self.tool_widget)
        self.tool_list = QTableWidget(0, 6)
        self.tool_list.setHorizontalHeaderLabels(["工具", "类型", "参数", "状态", "最近结果", "操作"])
        self.tool_list.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.tool_list.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
        self.tool_list.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.tool_list.verticalHeader().setVisible(False)
        self.tool_list.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        for column in range(1, 6):
            self.tool_list.horizontalHeader().setSectionResizeMode(column, QHeaderView.ResizeMode.ResizeToContents)
        self.tool_list.cellDoubleClicked.connect(self._list_detail)
        self.tool_views = QStackedWidget()
        self.tool_views.addWidget(self.scroll)
        self.tool_views.addWidget(self.tool_list)
        self.empty_tools = QLabel("未找到匹配工具\n请清除搜索或筛选条件后重试。")
        self.empty_tools.setObjectName("pageDescription")
        self.empty_tools.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.tool_views.addWidget(self.empty_tools)
        root.addWidget(self.tool_views, 1)
        self.card_view_button.clicked.connect(lambda: self._set_view_mode(0))
        self.list_view_button.clicked.connect(lambda: self._set_view_mode(1))
        self.console = TaskConsole()
        self.splitter.addWidget(tools_area)
        self.splitter.addWidget(self.console)
        self.splitter.setSizes([600, 400])
        self.splitter.setCollapsible(1, False)
        self.splitter.setStretchFactor(0, 3)
        self.splitter.setStretchFactor(1, 2)
        page_layout.addWidget(self.splitter)
        self._tools: list[dict[str, Any]] = []
        self._availability: dict[str, dict[str, Any]] = {}
        self._search = ""
        self._columns = 0
        self._execution_locked = False
        self._recent_tasks: dict[str, dict[str, Any]] = {}
        self._view_mode = 0

    def set_workspace(self, workspace: dict[str, Any], sdk_check: dict[str, Any] | None) -> None:
        self.app_combo.blockSignals(True)
        self.app_combo.clear()
        self.app_combo.addItems([str(item) for item in workspace.get("apps", [])])
        self.app_combo.setCurrentText(str(workspace.get("activeApp", "")))
        self.app_combo.blockSignals(False)
        state = str((sdk_check or {}).get("state", "idle"))
        items = (sdk_check or {}).get("items", [])
        ready = sum(1 for item in items if item.get("status") == "ready")
        attention = max(0, len(items) - ready)
        self.sdk.setText(f"{ready}/{len(items)} 通过" + (f"，{attention} 项需处理" if attention else "") if items else "等待检查")
        self.sdk.setProperty("status", "success" if state == "succeeded" else "warning")
        self.style().unpolish(self.sdk)
        self.style().polish(self.sdk)

    def set_tools(self, tools: list[dict[str, Any]], availability: dict[str, dict[str, Any]], search: str = "") -> None:
        self._tools = tools
        self._availability = availability
        self._search = search.strip().lower()
        self._render()

    def set_recent_tasks(self, tasks: list[dict[str, Any]]) -> None:
        recent: dict[str, dict[str, Any]] = {}
        for task in tasks:
            tool_id = str(task.get("toolId", ""))
            if tool_id and tool_id not in recent:
                recent[tool_id] = task
        self._recent_tasks = recent
        if self._tools:
            self._render()

    def set_execution_locked(self, locked: bool) -> None:
        if self._execution_locked == locked:
            return
        self._execution_locked = locked
        self._render()

    def _render(self) -> None:
        while self.tool_grid.count():
            item = self.tool_grid.takeAt(0)
            if item.widget():
                item.widget().hide()
                item.widget().deleteLater()
        categories = sorted({str(tool.get("category", "工具")) for tool in self._tools})
        current_category = str(self.category_filter.currentData() or "")
        if self.category_filter.count() != len(categories) + 1:
            self.category_filter.blockSignals(True)
            self.category_filter.clear()
            self.category_filter.addItem("全部类型", "")
            for category in categories:
                self.category_filter.addItem(category, category)
            index = self.category_filter.findData(current_category)
            self.category_filter.setCurrentIndex(max(0, index))
            self.category_filter.blockSignals(False)
        tools = self._filtered_tools()
        columns = self._column_count()
        self._columns = columns
        for column in range(3):
            self.tool_grid.setColumnStretch(column, 1 if column < columns else 0)
        for index, tool in enumerate(tools):
            check = self._availability.get(str(tool.get("id")), {})
            card = ToolCard(
                tool,
                bool(check.get("available", False)),
                str(check.get("message", "等待SDK环境检查")),
                self._execution_locked,
                self._recent_tasks.get(str(tool.get("id", ""))),
            )
            card.execute_requested.connect(self.execute_tool)
            card.detail_requested.connect(self.detail_tool)
            self.tool_grid.addWidget(card, index // columns, index % columns)
        self.count.setText(f"{len(tools)} 项")
        self._render_list(tools)
        self.tool_views.setCurrentIndex(self._view_mode if tools else 2)

    def _filtered_tools(self) -> list[dict[str, Any]]:
        category = str(self.category_filter.currentData() or "")
        status = str(self.status_filter.currentData() or "")
        tools = []
        for tool in self._tools:
            text = f"{tool.get('title', '')} {tool.get('description', '')} {tool.get('category', '')} {tool.get('id', '')}".lower()
            check = self._availability.get(str(tool.get("id")), {})
            available = bool(check.get("available", False))
            if self._search not in text or (category and tool.get("category") != category):
                continue
            if status == "available" and not available or status == "unavailable" and available:
                continue
            if status == "recent_failed" and str(self._recent_tasks.get(str(tool.get("id", "")), {}).get("state", "")) not in {"failed", "blocked"}:
                continue
            tools.append(tool)
        if self.sort_filter.currentData() == "recent":
            tools.sort(key=lambda tool: str(self._recent_tasks.get(str(tool.get("id", "")), {}).get("createdAt", "")), reverse=True)
        return tools

    def _render_list(self, tools: list[dict[str, Any]]) -> None:
        self.tool_list.setRowCount(len(tools))
        for row, tool in enumerate(tools):
            tool_id = str(tool.get("id", ""))
            check = self._availability.get(tool_id, {})
            available = bool(check.get("available", False))
            recent = self._recent_tasks.get(tool_id, {})
            values = [
                str(tool.get("title", tool_id)),
                str(tool.get("category", "工具")),
                {"none": "无需参数", "fixed": "固定参数", "dynamic": "本次参数"}.get(str(tool.get("parameterMode")), "-"),
                "就绪" if available else "不可运行",
                STATE_TEXT.get(str(recent.get("state", "")), "尚未运行"),
            ]
            for column, value in enumerate(values):
                item = QTableWidgetItem(value)
                item.setData(Qt.ItemDataRole.UserRole, tool_id)
                item.setToolTip(str(check.get("message", "")) if column == 3 else str(tool.get("description", "")))
                self.tool_list.setItem(row, column, item)
            action = primary_button(tool_action_text(tool, str(recent.get("state", ""))), "play")
            action.setEnabled(available and not self._execution_locked)
            action.clicked.connect(lambda _checked=False, value=tool: self.execute_tool.emit(value))
            self.tool_list.setCellWidget(row, 5, action)
            self.tool_list.setRowHeight(row, 46)

    def _set_view_mode(self, index: int) -> None:
        self._view_mode = index
        self.tool_views.setCurrentIndex(index if self._filtered_tools() else 2)
        for button, active in ((self.card_view_button, index == 0), (self.list_view_button, index == 1)):
            button.setProperty("active", active)
            button.style().unpolish(button)
            button.style().polish(button)

    def _list_detail(self, row: int, _column: int) -> None:
        item = self.tool_list.item(row, 0)
        if item is None:
            return
        tool_id = str(item.data(Qt.ItemDataRole.UserRole) or "")
        tool = next((value for value in self._tools if str(value.get("id")) == tool_id), None)
        if tool is not None:
            self.detail_tool.emit(tool)

    def resizeEvent(self, event: Any) -> None:
        super().resizeEvent(event)
        if self._tools:
            columns = self._column_count()
            if columns != self._columns:
                QTimer.singleShot(0, self._render)

    def showEvent(self, event: Any) -> None:
        super().showEvent(event)
        if self._tools:
            QTimer.singleShot(0, self._render)

    def _column_count(self) -> int:
        width = max(self.scroll.viewport().width(), self.width() - 56)
        return 3 if width > 1180 else 2 if width >= 760 else 1


class TaskConsole(QFrame):
    cancel_requested = Signal(str)
    rerun_requested = Signal(object)
    close_requested = Signal()

    def __init__(self) -> None:
        super().__init__()
        self.setObjectName("console")
        self.setMinimumHeight(240)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        root = QVBoxLayout(self)
        root.setContentsMargins(16, 12, 16, 14)
        root.setSpacing(10)
        header = QHBoxLayout()
        header.setSpacing(8)
        self.title = ElidedLabel("当前没有任务")
        self.title.setObjectName("toolTitle")
        self.title.setMinimumWidth(180)
        self.state = QLabel("空闲")
        self.state.setObjectName("taskStateBadge")
        self.state.setFixedWidth(72)
        self.state.setAlignment(Qt.AlignmentFlag.AlignCenter)
        header.addWidget(self.title, 3)
        header.addWidget(self.state)
        header.addStretch(1)
        self.rerun = QPushButton(icon("refresh", "#a8c6d8", 15), "再次运行")
        self.rerun.clicked.connect(lambda: self.rerun_requested.emit(self.task or {}))
        self.close_button = QPushButton(icon("close", "#a8c6d8", 15), "")
        self.close_button.setObjectName("iconButton")
        self.close_button.setToolTip("关闭任务详情")
        self.close_button.setAccessibleName("关闭任务详情")
        self.close_button.clicked.connect(self.close_requested)
        header.addWidget(self.rerun)
        header.addWidget(self.close_button)
        root.addLayout(header)

        self.meta = ElidedLabel("选择工具后可在这里查看任务摘要、问题、输出和产物")
        self.meta.setObjectName("muted")
        root.addWidget(self.meta)
        progress_row = QHBoxLayout()
        progress_row.setSpacing(8)
        self.stage = ElidedLabel("")
        self.stage.setObjectName("mono")
        self.progress = AnimatedProgressBar()
        self.progress.setRange(0, 100)
        self.progress.setValue(0)
        self.progress.setMaximumWidth(360)
        self.percent = QLabel("0%")
        self.percent.setObjectName("mono")
        self.percent.setFixedWidth(44)
        self.percent.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        self.ring = EnergyRing()
        self.cancel = QPushButton(icon("stop", "#ff93a7", 15), "停止")
        self.cancel.setObjectName("taskAction")
        self.cancel.clicked.connect(self._cancel)
        progress_row.addWidget(self.stage, 1)
        progress_row.addWidget(self.progress)
        progress_row.addWidget(self.percent)
        progress_row.addWidget(self.ring)
        progress_row.addWidget(self.cancel)
        root.addLayout(progress_row)

        self.tabs = QTabWidget()
        self.summary = QLabel()
        self.summary.setObjectName("taskSummary")
        self.summary.setWordWrap(True)
        self.summary.setAlignment(Qt.AlignmentFlag.AlignTop | Qt.AlignmentFlag.AlignLeft)
        self.summary.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        summary_host = QWidget()
        summary_layout = QVBoxLayout(summary_host)
        summary_layout.setContentsMargins(14, 14, 14, 14)
        summary_layout.addWidget(self.summary)
        summary_layout.addStretch(1)
        self.issues = QTableWidget(0, 3)
        self.issues.setHorizontalHeaderLabels(["级别", "来源", "问题与恢复建议"])
        self.issues.verticalHeader().setVisible(False)
        self.issues.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.issues.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.issues.horizontalHeader().setSectionResizeMode(2, QHeaderView.ResizeMode.Stretch)
        self.logs = LogView()
        self.logs.setObjectName("toolLog")
        output_host = QWidget()
        output_layout = QVBoxLayout(output_host)
        output_layout.setContentsMargins(0, 0, 0, 0)
        output_layout.setSpacing(6)
        output_tools = QHBoxLayout()
        self.output_search = QLineEdit()
        self.output_search.setPlaceholderText("在输出中查找")
        self.output_search.setClearButtonEnabled(True)
        self.output_search.returnPressed.connect(lambda: self.logs.find(self.output_search.text()))
        self.wrap_output = QCheckBox("自动换行")
        self.wrap_output.toggled.connect(lambda checked: self.logs.setLineWrapMode(QPlainTextEdit.LineWrapMode.WidgetWidth if checked else QPlainTextEdit.LineWrapMode.NoWrap))
        copy_output = QPushButton(icon("copy", "#a8c6d8", 15), "复制")
        copy_output.clicked.connect(self.logs.copy)
        output_tools.addWidget(self.output_search, 1)
        output_tools.addWidget(self.wrap_output)
        output_tools.addWidget(copy_output)
        output_layout.addLayout(output_tools)
        output_layout.addWidget(self.logs, 1)
        self.artifacts = QTableWidget(0, 3)
        self.artifacts.setHorizontalHeaderLabels(["产物", "类型", "路径"])
        self.artifacts.verticalHeader().setVisible(False)
        self.artifacts.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.artifacts.horizontalHeader().setSectionResizeMode(2, QHeaderView.ResizeMode.Stretch)
        self.tabs.addTab(summary_host, "摘要")
        self.tabs.addTab(self.issues, "问题 0")
        self.tabs.addTab(output_host, "输出")
        self.tabs.addTab(self.artifacts, "产物 0")
        root.addWidget(self.tabs, 1)
        self.task: dict[str, Any] | None = None
        self.set_task(None)

    def set_task(self, task: dict[str, Any] | None, timestamps: bool = True) -> None:
        self.task = task
        if task is None:
            self.title.set_full_text("当前没有任务")
            self.state.setText("空闲")
            self.stage.set_full_text("")
            self.progress.setValue(0)
            self.percent.setText("0%")
            self.logs.clear()
            self.meta.set_full_text("选择工具后可在这里查看任务摘要、问题、输出和产物")
            self.summary.setText("尚未运行任务。请从上方工具区选择一个操作。")
            self.issues.setRowCount(0)
            self.artifacts.setRowCount(0)
            self.tabs.setTabText(1, "问题 0")
            self.tabs.setTabText(3, "产物 0")
            self.ring.set_state("idle")
            self._apply_task_state("idle")
            return
        state = str(task.get("state", "queued"))
        percent = int(task.get("percent", 0))
        self.title.set_full_text(str(task.get("title", "任务")))
        self.stage.set_full_text(str(task.get("stage", "")))
        self.progress.setValue(percent)
        self.percent.setText(f"{percent}%")
        self.progress.set_active(state in {"queued", "running"})
        self.ring.set_state(state)
        self._apply_task_state(state)
        self.logs.set_logs(list(task.get("logs", [])), timestamps)
        self._refresh_details()
        self.tabs.setCurrentIndex(1 if state in {"failed", "blocked"} else 2 if state in {"queued", "running"} else 0)

    def update_progress(self, data: dict[str, Any]) -> None:
        if self.task is None or data.get("taskId") != self.task.get("id"):
            return
        self.task.update({key: data.get(key) for key in ("state", "percent", "stage", "message")})
        self.progress.setValue(int(data.get("percent", 0)))
        self.percent.setText(f"{int(data.get('percent', 0))}%")
        self.stage.set_full_text(str(data.get("stage", "")))
        state = str(data.get("state", "running"))
        self.progress.set_active(state in {"queued", "running"})
        self.ring.set_state(state)
        self._apply_task_state(state)
        self._refresh_details()

    def append_log(self, task_id: str, entry: dict[str, Any]) -> None:
        if self.task is not None and self.task.get("id") == task_id:
            self.logs.append_log(entry)
            self._refresh_details()

    def _cancel(self) -> None:
        if self.task and self.task.get("state") in {"queued", "running"}:
            self.cancel_requested.emit(str(self.task.get("id")))

    def _apply_task_state(self, state: str) -> None:
        self.state.setText(STATE_TEXT.get(state, "空闲" if state == "idle" else state))
        status = (
            "success" if state == "succeeded"
            else "error" if state in {"failed", "blocked"}
            else "warning" if state in {"queued", "running"}
            else "neutral"
        )
        self.state.setProperty("status", status)
        active = state in {"queued", "running"}
        self.rerun.setVisible(state in TERMINAL_STATES)
        self.close_button.setVisible(state not in {"queued", "running"})
        action = {
            "idle": ("等待", "clock", "#7893a9", False),
            "queued": ("停止", "stop", "#ff93a7", True),
            "running": ("停止", "stop", "#ff93a7", True),
            "succeeded": ("已完成", "check", "#3dd6a2", False),
            "failed": ("失败", "close", "#ff718b", False),
            "blocked": ("已阻止", "close", "#ff718b", False),
            "cancelled": ("已取消", "stop", "#91a7b8", False),
        }.get(state, (STATE_TEXT.get(state, state), "info", "#91a7b8", False))
        self.cancel.setText(action[0])
        self.cancel.setIcon(icon(action[1], action[2], 15))
        self.cancel.setProperty("taskState", state)
        self.cancel.setEnabled(action[3])
        for widget in (self.state, self.cancel):
            widget.style().unpolish(widget)
            widget.style().polish(widget)

    def _refresh_details(self) -> None:
        if self.task is None:
            return
        task = self.task
        logs = list(task.get("logs", []))
        errors = [entry for entry in logs if str(entry.get("level", "")).lower() == "error"]
        warnings = [entry for entry in logs if str(entry.get("level", "")).lower() == "warning"]
        successes = [entry for entry in logs if str(entry.get("level", "")).lower() == "success"]
        duration = task_duration_ms(task)
        started = str(task.get("startedAt") or task.get("createdAt") or "")
        started_text = started[11:19] if len(started) >= 19 else "--:--:--"
        self.meta.set_full_text(
            f"App：{task.get('app', '-') or '-'} · 开始：{started_text} · 耗时：{format_duration(duration)} · 阶段：{task.get('stage', '-') or '-'}"
        )
        state = str(task.get("state", "queued"))
        result = str(task.get("message", "")).strip()
        if state == "succeeded":
            headline = f"任务已完成，{len(errors)} 个错误，{len(warnings)} 个警告，{len(successes)} 条检查通过。"
            next_step = "可再次运行，或在“产物”中查看本次生成内容。"
        elif state in {"failed", "blocked"}:
            headline = f"任务未完成，发现 {len(errors)} 个错误和 {len(warnings)} 个警告。"
            next_step = "请先查看“问题”中的首个错误，处理后再重试。"
        elif state in {"queued", "running"}:
            headline = f"正在执行：{task.get('stage') or '准备任务'}，进度 {int(task.get('percent', 0))}%。"
            next_step = "实时输出可在“输出”中查看。"
        else:
            headline = "任务已取消。"
            next_step = "可再次运行此工具。"
        self.summary.setText(headline + (f"\n结果：{result}" if result else "") + f"\n下一步：{next_step}")
        issue_logs = errors + warnings
        self.issues.setRowCount(len(issue_logs))
        for row, entry in enumerate(issue_logs):
            level = str(entry.get("level", "warning")).lower()
            values = ["错误" if level == "error" else "警告", short_log_source(entry.get("source")), safe_text(entry.get("text", "")).strip()]
            for column, value in enumerate(values):
                item = QTableWidgetItem(value)
                item.setToolTip(value)
                if column == 0:
                    item.setForeground(QColor("#ff6b7a" if level == "error" else "#f2b84b"))
                self.issues.setItem(row, column, item)
        artifacts = list(task.get("artifacts", []))
        self.artifacts.setRowCount(len(artifacts))
        for row, artifact in enumerate(artifacts):
            for column, value in enumerate((artifact.get("name", ""), artifact.get("kind", "FILE"), artifact.get("relativePath", artifact.get("path", "")))):
                item = QTableWidgetItem(str(value))
                item.setToolTip(str(value))
                self.artifacts.setItem(row, column, item)
        self.tabs.setTabText(1, f"问题 {len(issue_logs)}")
        self.tabs.setTabText(3, f"产物 {len(artifacts)}")


class StudioLogView(LogView):
    """Read-only application health log shared by every page."""

    def __init__(self) -> None:
        super().__init__()
        self.setObjectName("studioLog")
        self.document().setMaximumBlockCount(200)
        self.setContextMenuPolicy(Qt.ContextMenuPolicy.NoContextMenu)


class GlobalLogPanel(QFrame):
    closed = Signal()

    def __init__(self) -> None:
        super().__init__()
        self.setObjectName("globalLogPanel")
        root = QVBoxLayout(self)
        root.setContentsMargins(12, 8, 12, 10)
        root.setSpacing(6)
        header = QHBoxLayout()
        title = QLabel("Studio 运行日志")
        title.setObjectName("toolTitle")
        self.filter = QComboBox()
        self.filter.addItem("全部", "")
        self.filter.addItem("信息", "info")
        self.filter.addItem("警告", "warning")
        self.filter.addItem("错误", "error")
        self.filter.currentIndexChanged.connect(self._render)
        copy_button = QPushButton(icon("copy", "#a8c6d8", 15), "复制诊断信息")
        copy_button.clicked.connect(self._copy)
        close_button = QPushButton(icon("close", "#a8c6d8", 15), "")
        close_button.setObjectName("iconButton")
        close_button.setToolTip("收起 Studio 日志")
        close_button.setAccessibleName("收起 Studio 日志")
        close_button.clicked.connect(self.closed)
        header.addWidget(title)
        header.addStretch(1)
        header.addWidget(self.filter)
        header.addWidget(copy_button)
        header.addWidget(close_button)
        root.addLayout(header)
        self.log = StudioLogView()
        root.addWidget(self.log, 1)
        self.entries: list[dict[str, Any]] = []
        self.context: dict[str, str] = {}

    def append_log(self, entry: dict[str, Any]) -> None:
        self.entries.append(entry)
        if len(self.entries) > 200:
            self.entries = self.entries[-200:]
        level = str(self.filter.currentData() or "")
        if self._matches(entry, level):
            self.log.append_log(entry)

    def _render(self) -> None:
        level = str(self.filter.currentData() or "")
        self.log.set_logs([entry for entry in self.entries if self._matches(entry, level)])

    @staticmethod
    def _matches(entry: dict[str, Any], level: str) -> bool:
        entry_level = str(entry.get("level", "")).lower()
        return not level or entry_level == level or level == "info" and entry_level in {"debug", "success"}

    def set_context(self, workspace: dict[str, Any]) -> None:
        self.context = {
            "Studio": __version__,
            "SDK": str(workspace.get("path", "")),
            "App": str(workspace.get("activeApp", "")),
        }

    def _copy(self) -> None:
        heading = "\n".join(f"{key}: {value or '-'}" for key, value in self.context.items())
        QApplication.clipboard().setText(heading + "\n\n" + self.log.toPlainText())


class StudioStatusBar(QFrame):
    toggle_logs = Signal()

    def __init__(self) -> None:
        super().__init__()
        self.setObjectName("studioStatusBar")
        self.setFixedHeight(32)
        layout = QHBoxLayout(self)
        layout.setContentsMargins(12, 0, 8, 0)
        layout.setSpacing(16)
        self.brain = QLabel("Brain 连接中")
        self.queue = QLabel("运行队列 0")
        self.warning = QLabel("警告 0")
        self.error = QLabel("错误 0")
        self.logs = QPushButton(icon("terminal", "#a8c6d8", 14), "展开 Studio 日志")
        self.logs.setProperty("statusAction", True)
        self.logs.clicked.connect(self.toggle_logs)
        layout.addWidget(self.brain)
        layout.addWidget(self.queue)
        layout.addStretch(1)
        layout.addWidget(self.warning)
        layout.addWidget(self.error)
        layout.addWidget(self.logs)

    def set_brain(self, online: bool) -> None:
        self.brain.setText("Brain 在线" if online else "Brain 离线")
        self.brain.setProperty("status", "success" if online else "error")
        self.brain.style().unpolish(self.brain)
        self.brain.style().polish(self.brain)

    def set_running(self, running: bool) -> None:
        self.queue.setText(f"运行队列 {1 if running else 0}")

    def set_counts(self, warnings: int, errors: int) -> None:
        self.warning.setText(f"警告 {warnings}")
        self.error.setText(f"错误 {errors}")


class RecentPage(QWidget):
    clear_requested = Signal()

    def __init__(self) -> None:
        super().__init__()
        root = QVBoxLayout(self)
        configure_page_layout(root)
        clear = QPushButton(icon("trash", "#b5cad8", 15), "清除已完成记录")
        clear.clicked.connect(self.clear_requested)
        root.addWidget(page_heading("最近任务", "选择最多两个任务并排比较状态、校验结果和结构化日志。", clear))
        splitter = QSplitter(Qt.Orientation.Vertical)
        self.table = QTableWidget(0, 5)
        self.table.setHorizontalHeaderLabels(["状态", "任务", "App", "进度", "创建时间"])
        self.table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.table.setSelectionMode(QAbstractItemView.SelectionMode.MultiSelection)
        self.table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.table.verticalHeader().setVisible(False)
        self.table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
        self.table.itemSelectionChanged.connect(self._compare)
        splitter.addWidget(self.table)
        compare = QSplitter(Qt.Orientation.Horizontal)
        self.left = LogView()
        self.right = LogView()
        compare.addWidget(self.left)
        compare.addWidget(self.right)
        compare.setSizes([500, 500])
        splitter.addWidget(compare)
        splitter.setSizes([300, 360])
        root.addWidget(splitter, 1)
        self.tasks: list[dict[str, Any]] = []

    def set_tasks(self, tasks: list[dict[str, Any]], timestamps: bool = True) -> None:
        selected_ids = {self.tasks[index.row()].get("id") for index in self.table.selectionModel().selectedRows() if index.row() < len(self.tasks)}
        self.tasks = tasks
        self.table.setRowCount(len(tasks))
        for row, task in enumerate(tasks):
            values = [
                STATE_TEXT.get(str(task.get("state")), str(task.get("state"))),
                str(task.get("title", "")),
                str(task.get("app", "")),
                f"{int(task.get('percent', 0))}%",
                str(task.get("createdAt", ""))[:19].replace("T", " "),
            ]
            for column, value in enumerate(values):
                self.table.setItem(row, column, QTableWidgetItem(value))
            if task.get("id") in selected_ids:
                self.table.selectRow(row)
        self.timestamps = timestamps
        self._compare()

    def _compare(self) -> None:
        rows = sorted({index.row() for index in self.table.selectionModel().selectedRows()})
        if len(rows) > 2:
            self.table.selectRow(rows[-1])
            rows = rows[-2:]
        selected = [self.tasks[row] for row in rows if row < len(self.tasks)]
        self.left.set_logs(list(selected[0].get("logs", [])) if selected else [], getattr(self, "timestamps", True))
        self.right.set_logs(list(selected[1].get("logs", [])) if len(selected) > 1 else [], getattr(self, "timestamps", True))


class TerminalView(QPlainTextEdit):
    bytes_requested = Signal(object)
    text_requested = Signal(str)
    enter_requested = Signal()

    KEY_SEQUENCES = {
        Qt.Key.Key_Up: b"\x1b[A",
        Qt.Key.Key_Down: b"\x1b[B",
        Qt.Key.Key_Right: b"\x1b[C",
        Qt.Key.Key_Left: b"\x1b[D",
        Qt.Key.Key_Home: b"\x1b[H",
        Qt.Key.Key_End: b"\x1b[F",
        Qt.Key.Key_Delete: b"\x1b[3~",
        Qt.Key.Key_PageUp: b"\x1b[5~",
        Qt.Key.Key_PageDown: b"\x1b[6~",
    }

    def __init__(self) -> None:
        super().__init__()
        self.setReadOnly(True)
        self.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setAttribute(Qt.WidgetAttribute.WA_InputMethodEnabled, True)
        self._direct = True
        self._opened = False

    def set_terminal_state(self, opened: bool, direct: bool) -> None:
        self._opened = opened
        self._direct = direct
        self.setProperty("terminalActive", opened and direct)
        self.style().unpolish(self)
        self.style().polish(self)

    def keyPressEvent(self, event: QKeyEvent) -> None:
        if not (self._opened and self._direct):
            super().keyPressEvent(event)
            return
        modifiers = event.modifiers()
        control = bool(modifiers & Qt.KeyboardModifier.ControlModifier)
        shift = bool(modifiers & Qt.KeyboardModifier.ShiftModifier)
        if control and shift and event.key() == Qt.Key.Key_C:
            self.copy()
            event.accept()
            return
        if control and shift and event.key() == Qt.Key.Key_V:
            text = QApplication.clipboard().text()
            if text:
                self.text_requested.emit(text)
            event.accept()
            return
        if control and Qt.Key.Key_A <= event.key() <= Qt.Key.Key_Z:
            self.bytes_requested.emit(bytes((event.key() - Qt.Key.Key_A + 1,)))
            event.accept()
            return
        if event.key() in self.KEY_SEQUENCES:
            self.bytes_requested.emit(self.KEY_SEQUENCES[event.key()])
            event.accept()
            return
        if event.key() in {Qt.Key.Key_Return, Qt.Key.Key_Enter}:
            self.enter_requested.emit()
            event.accept()
            return
        if event.key() == Qt.Key.Key_Backspace:
            self.bytes_requested.emit(b"\x7f")
            event.accept()
            return
        if event.key() == Qt.Key.Key_Tab:
            self.bytes_requested.emit(b"\x09")
            event.accept()
            return
        if event.key() == Qt.Key.Key_Escape:
            self.bytes_requested.emit(b"\x1b")
            event.accept()
            return
        text = event.text()
        if text and not control and not (modifiers & Qt.KeyboardModifier.AltModifier):
            self.text_requested.emit(text)
            event.accept()
            return
        super().keyPressEvent(event)

    def inputMethodEvent(self, event: Any) -> None:
        if self._opened and self._direct and event.commitString():
            self.text_requested.emit(event.commitString())
            event.accept()
            return
        super().inputMethodEvent(event)


class SerialPage(QWidget):
    open_requested = Signal(str, int)
    close_requested = Signal()
    write_requested = Signal(str)

    def __init__(self) -> None:
        super().__init__()
        root = QVBoxLayout(self)
        configure_page_layout(root)
        root.addWidget(page_heading("串口终端", "连接串口后点击终端区域，可像命令行一样直接输入。"))
        controls = QFrame()
        controls.setObjectName("panel")
        row = QHBoxLayout(controls)
        self.port = QComboBox()
        self.baud = QComboBox()
        self.baud.setEditable(True)
        self.baud.addItems(["9600", "115200", "460800", "921600"])
        self.baud.setCurrentText("115200")
        self.encoding = QComboBox()
        self.encoding.addItems(["UTF-8", "GBK", "ASCII", "HEX"])
        self.line_ending = QComboBox()
        self.line_ending.addItem("无换行", "")
        self.line_ending.addItem("CR", "\r")
        self.line_ending.addItem("LF", "\n")
        self.line_ending.addItem("CRLF", "\r\n")
        self.line_ending.setCurrentIndex(3)
        refresh = QPushButton(icon("refresh", "#a7c5d7", 15), "刷新")
        refresh.clicked.connect(self.refresh_ports)
        self.connect_button = primary_button("连接", "terminal")
        self.connect_button.clicked.connect(self._toggle)
        self.pause = QCheckBox("暂停滚动")
        self.direct = QCheckBox("键盘直连")
        self.direct.setChecked(True)
        self.local_echo = QCheckBox("本地回显")
        row.addWidget(QLabel("端口"))
        row.addWidget(self.port)
        row.addWidget(refresh)
        row.addWidget(QLabel("波特率"))
        row.addWidget(self.baud)
        row.addWidget(QLabel("显示"))
        row.addWidget(self.encoding)
        row.addWidget(QLabel("结尾"))
        row.addWidget(self.line_ending)
        row.addWidget(self.direct)
        row.addWidget(self.local_echo)
        row.addWidget(self.pause)
        row.addStretch()
        row.addWidget(self.connect_button)
        root.addWidget(controls)
        self.output = TerminalView()
        self.output.setToolTip("连接后点击此区域输入；Ctrl+Shift+C/V用于复制粘贴")
        self.output.bytes_requested.connect(self._send_payload)
        self.output.text_requested.connect(self._send_direct_text)
        self.output.enter_requested.connect(self._send_enter)
        self.direct.toggled.connect(lambda checked: self.output.set_terminal_state(self.opened, checked))
        root.addWidget(self.output, 1)
        send_row = QHBoxLayout()
        self.input = QLineEdit()
        self.input.setPlaceholderText("输入要发送的数据")
        self.input.returnPressed.connect(self._send)
        send = primary_button("发送", "send")
        send.clicked.connect(self._send)
        clear = QPushButton(icon("trash", "#a7c5d7", 15), "清屏")
        clear.clicked.connect(self.output.clear)
        export = QPushButton(icon("copy", "#a7c5d7", 15), "导出")
        export.clicked.connect(self._export)
        send_row.addWidget(self.input, 1)
        send_row.addWidget(send)
        send_row.addWidget(clear)
        send_row.addWidget(export)
        root.addLayout(send_row)
        self.opened = False
        self.output.set_terminal_state(False, self.direct.isChecked())
        self.refresh_ports()

    def refresh_ports(self) -> None:
        current = self.port.currentText()
        self.port.clear()
        self.port.addItems(serial_ports())
        if current:
            self.port.setCurrentText(current)

    def set_snapshot(self, snapshot: dict[str, Any]) -> None:
        self.opened = bool(snapshot.get("opened", False))
        self.connect_button.setText("断开" if self.opened else "连接")
        if snapshot.get("port"):
            self.port.setCurrentText(str(snapshot["port"]))
        self.port.setEnabled(not self.opened)
        self.baud.setEnabled(not self.opened)
        self.output.set_terminal_state(self.opened, self.direct.isChecked())
        if self.opened and self.direct.isChecked():
            self.output.setFocus()

    def append_serial(self, direction: str, data: dict[str, Any]) -> None:
        raw_hex = str(data.get("dataHex", ""))
        try:
            payload = bytes.fromhex(raw_hex)
        except ValueError:
            payload = b""
        mode = self.encoding.currentText()
        if mode == "HEX":
            text = payload.hex(" ").upper() + (" " if payload else "")
        elif mode == "ASCII":
            text = self._ascii_text(payload)
        else:
            text = payload.decode("gbk" if mode == "GBK" else "utf-8", errors="replace")
        if direction == "tx" and not self.local_echo.isChecked():
            return
        cursor = self.output.textCursor()
        cursor.movePosition(QTextCursor.MoveOperation.End)
        cursor.insertText(text)
        self.output.setTextCursor(cursor)
        if not self.pause.isChecked():
            self.output.verticalScrollBar().setValue(self.output.verticalScrollBar().maximum())

    def append_error(self, message: str) -> None:
        self.output.appendPlainText(f"\n[ERROR] {message}")

    @staticmethod
    def _ascii_text(payload: bytes) -> str:
        escapes = {8: "\\b", 9: "\\t", 10: "\\n\n", 13: "\\r"}
        return "".join(chr(value) if 32 <= value <= 126 else escapes.get(value, f"\\x{value:02X}") for value in payload)

    def _toggle(self) -> None:
        if self.opened:
            self.close_requested.emit()
        else:
            self.open_requested.emit(self.port.currentText(), int(self.baud.currentText() or 115200))

    def _send(self) -> None:
        text = self.input.text()
        if not text:
            return
        if self.encoding.currentText() == "HEX":
            try:
                payload = bytes.fromhex(text)
            except ValueError:
                self.append_error("十六进制输入格式无效")
                return
        else:
            payload = self._encode_text(text + str(self.line_ending.currentData() or ""))
            if payload is None:
                return
        self._send_payload(payload)
        self.input.clear()

    def _send_direct_text(self, text: str) -> None:
        payload = self._encode_text(text)
        if payload is not None:
            self._send_payload(payload)

    def _send_enter(self) -> None:
        ending = str(self.line_ending.currentData() or "\r")
        self._send_payload(ending.encode("ascii"))

    def _encode_text(self, text: str) -> bytes | None:
        mode = self.encoding.currentText()
        encoding = "gbk" if mode == "GBK" else "ascii" if mode == "ASCII" else "utf-8"
        try:
            return text.encode(encoding)
        except UnicodeEncodeError:
            self.append_error(f"当前{mode}模式无法编码输入内容")
            return None

    def _send_payload(self, payload: bytes) -> None:
        if not self.opened:
            self.append_error("串口尚未连接")
            return
        if payload:
            self.write_requested.emit(payload.hex())

    def _export(self) -> None:
        path = QFileDialog.getSaveFileName(self, "导出串口日志", "serial-session.log", "Log (*.log);;Text (*.txt)")[0]
        if path:
            Path(path).write_text(self.output.toPlainText(), encoding="utf-8")


class ArtifactsPage(QWidget):
    refresh_requested = Signal()
    preview_requested = Signal(str)

    def __init__(self) -> None:
        super().__init__()
        root = QVBoxLayout(self)
        configure_page_layout(root)
        refresh = QPushButton(icon("refresh", "#a8c6d8", 16), "刷新产物")
        refresh.clicked.connect(self.refresh_requested)
        root.addWidget(page_heading("构建产物", "集中查看固件、MAP、日志、DTS 生成文件和工程交付文件。", refresh))
        splitter = QSplitter(Qt.Orientation.Horizontal)
        self.table = QTableWidget(0, 4)
        self.table.setHorizontalHeaderLabels(["类型", "名称", "大小", "相对路径"])
        self.table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.table.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
        self.table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.table.verticalHeader().setVisible(False)
        self.table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
        self.table.horizontalHeader().setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
        self.table.itemSelectionChanged.connect(self._selected)
        self.preview = QPlainTextEdit()
        self.preview.setReadOnly(True)
        splitter.addWidget(self.table)
        splitter.addWidget(self.preview)
        splitter.setSizes([520, 580])
        root.addWidget(splitter, 1)
        actions = QHBoxLayout()
        open_folder = QPushButton(icon("folder", "#a8c6d8", 15), "在资源管理器中显示")
        open_folder.clicked.connect(self._open)
        copy_path = QPushButton(icon("copy", "#a8c6d8", 15), "复制路径")
        copy_path.clicked.connect(self._copy)
        actions.addWidget(open_folder)
        actions.addWidget(copy_path)
        actions.addStretch()
        root.addLayout(actions)
        self.artifacts: list[dict[str, Any]] = []

    def set_artifacts(self, artifacts: list[dict[str, Any]]) -> None:
        self.artifacts = artifacts
        self.table.setRowCount(len(artifacts))
        for row, artifact in enumerate(artifacts):
            values = [artifact.get("kind", "FILE"), artifact.get("name", ""), human_size(int(artifact.get("size", 0))), artifact.get("relativePath", "")]
            for column, value in enumerate(values):
                item = QTableWidgetItem(str(value))
                item.setData(Qt.ItemDataRole.UserRole, artifact.get("id"))
                self.table.setItem(row, column, item)

    def show_preview(self, value: dict[str, Any]) -> None:
        suffix = "\n\n[预览已截断]" if value.get("truncated") else ""
        self.preview.setPlainText(str(value.get("text", "")) + suffix)

    def _current(self) -> dict[str, Any] | None:
        row = self.table.currentRow()
        return self.artifacts[row] if 0 <= row < len(self.artifacts) else None

    def _selected(self) -> None:
        artifact = self._current()
        if artifact is None:
            return
        if artifact.get("textPreview"):
            self.preview_requested.emit(str(artifact.get("id")))
        else:
            self.preview.setPlainText(f"{artifact.get('kind')} 二进制文件\n{artifact.get('path')}\n大小：{human_size(int(artifact.get('size', 0)))}")

    def _open(self) -> None:
        artifact = self._current()
        if artifact:
            path = Path(str(artifact.get("path")))
            if os.name == "nt":
                os.startfile(str(path.parent))
            else:
                QDesktopServices.openUrl(path.parent.as_uri())

    def _copy(self) -> None:
        artifact = self._current()
        if artifact:
            QApplication.clipboard().setText(str(artifact.get("path", "")))


class SDKCheckPage(QWidget):
    deep_requested = Signal()

    def __init__(self) -> None:
        super().__init__()
        root = QVBoxLayout(self)
        configure_page_layout(root)
        self.deep_button = QPushButton(icon("refresh", "#a8c6d8", 16), "只读深检")
        self.deep_button.clicked.connect(self.deep_requested)
        root.addWidget(page_heading("SDK 环境检查", "自动快检工作区；手动深检保持只读，不执行编译、烧录或清理。", self.deep_button))
        progress_row = QHBoxLayout()
        self.stage = QLabel("等待检查")
        self.stage.setObjectName("toolTitle")
        self.progress = AnimatedProgressBar()
        self.progress.setMaximumWidth(520)
        self.percent = QLabel("0%")
        self.percent.setObjectName("mono")
        self.ring = EnergyRing()
        progress_row.addWidget(self.stage)
        progress_row.addStretch(1)
        progress_row.addWidget(self.progress, 1)
        progress_row.addWidget(self.percent)
        progress_row.addWidget(self.ring)
        root.addLayout(progress_row)
        splitter = QSplitter(Qt.Orientation.Horizontal)
        self.items = QTableWidget(0, 3)
        self.items.setHorizontalHeaderLabels(["状态", "检查项", "耗时"])
        self.items.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.items.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
        self.items.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.items.setTextElideMode(Qt.TextElideMode.ElideRight)
        self.items.verticalHeader().setVisible(False)
        self.items.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.ResizeToContents)
        self.items.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
        self.items.horizontalHeader().setSectionResizeMode(2, QHeaderView.ResizeMode.ResizeToContents)
        self.items.currentCellChanged.connect(lambda row, _column, _old_row, _old_column: self._detail(row))
        self.logs = LogView()
        splitter.addWidget(self.items)
        splitter.addWidget(self.logs)
        splitter.setSizes([350, 850])
        root.addWidget(splitter, 1)
        self.snapshot: dict[str, Any] = {}

    def set_snapshot(self, snapshot: dict[str, Any]) -> None:
        self.snapshot = snapshot
        percent = int(snapshot.get("percent", 0))
        self.progress.setValue(percent)
        self.progress.set_active(snapshot.get("state") == "running")
        self.ring.set_state(str(snapshot.get("state", "idle")))
        self.percent.setText(f"{percent}%")
        running = snapshot.get("state") == "running"
        self.deep_button.setEnabled(not running)
        self.deep_button.setText("深检中..." if running and snapshot.get("mode") == "deep" else "只读深检")
        state_text = {"idle": "等待", "running": "检查中", "succeeded": "完成", "failed": "失败"}.get(str(snapshot.get("state", "idle")), str(snapshot.get("state", "idle")))
        self.stage.setText(f"{'只读深检' if snapshot.get('mode') == 'deep' else '自动快检'} · {state_text}")
        items = list(snapshot.get("items", []))
        self.items.setRowCount(len(items))
        for row_index, item in enumerate(items):
            status = str(item.get("status", "unknown"))
            icon_name = {"ready": "check", "warning": "warning", "error": "close", "unknown": "info"}.get(status, "info")
            color = {"ready": "#42e0ae", "warning": "#ffd166", "error": "#ff6f88", "unknown": "#829db2"}.get(status, "#829db2")
            values = [
                ({"ready": "正常", "warning": "警告", "error": "错误"}.get(status, "未知"), icon(icon_name, color, 16)),
                (str(item.get("title", "")), None),
                (f"{item.get('durationMs', 0)} ms", None),
            ]
            for column, (text_value, item_icon) in enumerate(values):
                cell = QTableWidgetItem(item_icon, text_value) if item_icon is not None else QTableWidgetItem(text_value)
                cell.setToolTip(str(item.get("message", "")))
                if column == 0:
                    cell.setForeground(QColor(color))
                self.items.setItem(row_index, column, cell)
        self.logs.set_logs(list(snapshot.get("logs", [])))

    def _detail(self, row: int) -> None:
        items = self.snapshot.get("items", [])
        if 0 <= row < len(items):
            item = items[row]
            related = [entry for entry in self.snapshot.get("logs", []) if str(entry.get("source", "")) in {str(item.get("id", "")), "sdk_check"}]
            self.logs.set_logs(related or list(self.snapshot.get("logs", [])))


class SettingsPage(QWidget):
    save_requested = Signal(dict)
    choose_workspace = Signal()

    def __init__(self, ui_state: UiState) -> None:
        super().__init__()
        self.ui_state = ui_state
        root = QVBoxLayout(self)
        configure_page_layout(root)
        root.addWidget(page_heading("设置", "本机状态保存在 LocalAppData，不把机器路径写回 SDK 仓库。"))
        panel = QFrame()
        panel.setObjectName("panel")
        panel.setMaximumWidth(820)
        form = QFormLayout(panel)
        self.workspace = QLineEdit()
        self.workspace.setReadOnly(True)
        choose = QPushButton(icon("folder", "#a8c6d8", 15), "选择工作区")
        choose.clicked.connect(self.choose_workspace)
        path = QWidget()
        path_layout = QHBoxLayout(path)
        path_layout.setContentsMargins(0, 0, 0, 0)
        path_layout.addWidget(self.workspace, 1)
        path_layout.addWidget(choose)
        self.probe = QComboBox()
        self.probe.addItem("CMSIS-DAP", "dap")
        self.probe.addItem("ST-Link", "stlink")
        self.retention = QSpinBox()
        self.retention.setRange(10, 1000)
        self.timestamps = QCheckBox("显示结构化日志时间戳")
        self.notify = QCheckBox("窗口不活跃时发送任务完成通知")
        form.addRow("活动工作区", path)
        form.addRow("默认下载探针", self.probe)
        form.addRow("保留任务记录", self.retention)
        form.addRow("日志", self.timestamps)
        form.addRow("系统通知", self.notify)
        save = primary_button("保存设置", "check")
        save.clicked.connect(self._save)
        form.addRow("", save)
        root.addWidget(panel)
        root.addStretch()

    def set_values(self, workspace: dict[str, Any], settings: dict[str, Any]) -> None:
        self.workspace.setText(str(workspace.get("path", "")))
        self.probe.setCurrentIndex(max(0, self.probe.findData(settings.get("default_probe", "dap"))))
        self.retention.setValue(int(settings.get("log_retention", 100)))
        self.timestamps.setChecked(bool(settings.get("log_timestamps", True)))
        self.notify.setChecked(bool(self.ui_state.values.get("notifyOnFinish", True)))

    def _save(self) -> None:
        self.ui_state.values["notifyOnFinish"] = self.notify.isChecked()
        self.ui_state.save()
        self.save_requested.emit({
            "default_probe": self.probe.currentData(),
            "log_retention": self.retention.value(),
            "log_timestamps": self.timestamps.isChecked(),
        })


class MainWindow(QMainWindow):
    def __init__(self, workspace: Path | None = None) -> None:
        super().__init__()
        self.setWindowTitle("ARK CREW Studio")
        self.setWindowIcon(app_icon())
        self.setMinimumSize(1080, 680)
        self.resize(1440, 900)
        self.ui_state = UiState()
        self.workspace: dict[str, Any] = {"opened": False, "path": "", "settings": {}}
        self.settings: dict[str, Any] = {}
        self.tools: list[dict[str, Any]] = []
        self.tasks: list[dict[str, Any]] = []
        self.sdk_check: dict[str, Any] = {}
        self.serial_snapshot: dict[str, Any] = {}
        self._task_by_id: dict[str, dict[str, Any]] = {}
        self._closing = False
        self._last_brain_online: bool | None = None
        self._task_running = False
        self._studio_warning_count = 0
        self._studio_error_count = 0
        self.file_watcher = QFileSystemWatcher(self)
        self.file_watcher.directoryChanged.connect(self._workspace_changed)
        self.file_watcher.fileChanged.connect(self._workspace_changed)
        self._build_ui()
        self.brain = BrainClient(workspace)
        self.brain.event_received.connect(self._event)
        self.brain.backend_error.connect(self._error)
        self.brain.state_changed.connect(self._brain_state)
        self.brain.start()
        self._restore_window()

    def _build_ui(self) -> None:
        root = QWidget()
        root.setObjectName("root")
        self.setCentralWidget(root)
        root_layout = QVBoxLayout(root)
        root_layout.setContentsMargins(0, 0, 0, 0)
        root_layout.setSpacing(0)
        body = QWidget()
        outer = QHBoxLayout(body)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)
        self.sidebar = QFrame()
        self.sidebar.setObjectName("sidebar")
        self.sidebar.setFixedWidth(192)
        side_layout = QVBoxLayout(self.sidebar)
        side_layout.setContentsMargins(10, 12, 10, 12)
        self.nav_buttons: dict[str, QPushButton] = {}
        for key, label, icon_name in (
            ("workbench", "工程控制台", "dashboard"),
            ("recent", "最近任务", "history"),
            ("serial", "串口终端", "terminal"),
            ("artifacts", "构建产物", "package"),
            ("settings", "设置", "settings"),
        ):
            button = QPushButton(icon(icon_name, "#9fc5dc", 18), label)
            button.setProperty("nav", True)
            button.clicked.connect(lambda _checked=False, name=key: self.set_page(name))
            self.nav_buttons[key] = button
            side_layout.addWidget(button)
        side_layout.addStretch()
        outer.addWidget(self.sidebar)
        self.main_host = QWidget()
        self.main_host.setObjectName("mainContent")
        self.main_host.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
        main_layout = QVBoxLayout(self.main_host)
        main_layout.setContentsMargins(0, 0, 0, 0)
        main_layout.setSpacing(0)
        self.pages = FadeStackedWidget()
        self.workbench = WorkbenchPage()
        self.recent = RecentPage()
        self.serial_page = SerialPage()
        self.artifacts = ArtifactsPage()
        self.sdk_page = SDKCheckPage()
        self.settings_page = SettingsPage(self.ui_state)
        self.welcome = self._welcome_page()
        self.page_map = {
            "welcome": self.welcome,
            "workbench": self.workbench,
            "recent": self.recent,
            "serial": self.serial_page,
            "artifacts": self.artifacts,
            "sdk-check": self.sdk_page,
            "settings": self.settings_page,
        }
        for page in self.page_map.values():
            page.setProperty("studioPage", True)
            page.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
            self.pages.addWidget(page)
        self.console = self.workbench.console
        self.main_splitter = self.workbench.splitter
        sizes = list(self.ui_state.values.get("mainSplitter", [600, 400]))
        total = sum(sizes) if len(sizes) == 2 else 0
        ratio = sizes[1] / total if total else 0.0
        if len(sizes) != 2 or not 0.35 <= ratio <= 0.45:
            sizes = [600, 400]
        self.main_splitter.setSizes(sizes)
        main_layout.addWidget(self.pages)
        outer.addWidget(self.main_host, 1)
        root_layout.addWidget(body, 1)
        self.global_log_panel = GlobalLogPanel()
        self.global_log_panel.setMinimumHeight(180)
        self.global_log_panel.setMaximumHeight(300)
        self.global_log_panel.hide()
        self.global_log_panel.closed.connect(lambda: self._toggle_global_logs(False))
        root_layout.addWidget(self.global_log_panel)
        self.studio_log = self.global_log_panel.log
        self.status_bar = StudioStatusBar()
        self.status_bar.toggle_logs.connect(lambda: self._toggle_global_logs(not self.global_log_panel.isVisible()))
        root_layout.addWidget(self.status_bar)
        self._menus()
        self._tray()
        self.workbench.app_changed.connect(self._select_app)
        self.workbench.execute_tool.connect(self.execute_tool)
        self.workbench.detail_tool.connect(self.detail_tool)
        self.workbench.sdk_check_requested.connect(lambda: self.set_page("sdk-check"))
        self.workbench.open_project_requested.connect(self._open_project)
        self.console.cancel_requested.connect(lambda task_id: self.brain.request("task.cancel", {"taskId": task_id}))
        self.console.rerun_requested.connect(self._rerun_task)
        self.console.close_requested.connect(lambda: self._toggle_monitor(False))
        self.recent.clear_requested.connect(lambda: self.brain.request("task.history.clear", {}, lambda _value: self._refresh_tasks()))
        self.serial_page.open_requested.connect(lambda port, baud: self.brain.request("serial.open", {"port": port, "baud": baud}, self.serial_page.set_snapshot, self._error))
        self.serial_page.close_requested.connect(lambda: self.brain.request("serial.close", {}, self.serial_page.set_snapshot, self._error))
        self.serial_page.write_requested.connect(lambda data: self.brain.request("serial.write", {"dataHex": data}, error=self._error))
        self.artifacts.refresh_requested.connect(self._refresh_artifacts)
        self.artifacts.preview_requested.connect(lambda artifact_id: self.brain.request("artifact.read", {"artifactId": artifact_id}, self.artifacts.show_preview, self._error))
        self.sdk_page.deep_requested.connect(lambda: self.brain.request("sdk_check.start", {"force": True, "mode": "deep", "reloadRegistry": True, "reason": "用户手动深度检查"}, error=self._error))
        self.settings_page.save_requested.connect(lambda values: self.brain.request("settings.update", {"values": values}, self._settings_saved, self._error))
        self.settings_page.choose_workspace.connect(self.choose_workspace)
        self.overlay = ToolOverlay(root)
        self.overlay.execute_requested.connect(self._start_from_overlay)
        self._studio_log("info", "studio", "ARK CREW Studio 已启动，正在连接 Brain")
        self.set_page("welcome")

    def _menus(self) -> None:
        self.menuBar().setFixedHeight(42)
        file_menu = self.menuBar().addMenu("文件")
        open_action = QAction(icon("folder", "#a8c6d8", 16), "打开 SDK 工作区", self)
        open_action.setShortcut(QKeySequence.StandardKey.Open)
        open_action.triggered.connect(self.choose_workspace)
        file_menu.addAction(open_action)
        self.recent_menu = file_menu.addMenu("最近工作区")
        file_menu.addSeparator()
        exit_action = QAction("退出", self)
        exit_action.setShortcut(QKeySequence.StandardKey.Quit)
        exit_action.triggered.connect(self.close)
        file_menu.addAction(exit_action)
        self.tool_menu = self.menuBar().addMenu("工具")
        view_menu = self.menuBar().addMenu("视图")
        for key, label in (("workbench", "工程控制台"), ("recent", "最近任务"), ("serial", "串口终端"), ("artifacts", "构建产物"), ("sdk-check", "SDK 环境检查"), ("settings", "设置")):
            action = QAction(label, self)
            action.triggered.connect(lambda _checked=False, name=key: self.set_page(name))
            view_menu.addAction(action)
        view_menu.addSeparator()
        self.monitor_action = QAction("显示任务监控", self)
        self.monitor_action.setCheckable(True)
        self.monitor_action.setChecked(True)
        self.monitor_action.toggled.connect(self._toggle_monitor)
        view_menu.addAction(self.monitor_action)
        self.global_log_action = QAction("展开 Studio 日志", self)
        self.global_log_action.setCheckable(True)
        self.global_log_action.toggled.connect(self._toggle_global_logs)
        view_menu.addAction(self.global_log_action)
        help_menu = self.menuBar().addMenu("帮助")
        about = QAction("关于 ARK CREW Studio", self)
        about.triggered.connect(lambda: QMessageBox.about(self, "关于 ARK CREW Studio", f"ARK CREW Studio {__version__}\nPySide6 + Python Brain\nARK CREW Embedded SDK 本地开发工作台"))
        help_menu.addAction(about)
        self.search_host = QWidget(self.menuBar())
        self.search_host.setObjectName("searchHost")
        search_layout = QHBoxLayout(self.search_host)
        search_layout.setContentsMargins(0, 0, 0, 0)
        self.search = QLineEdit()
        self.search.setObjectName("toolSearch")
        self.search.setPlaceholderText("搜索工具")
        self.search.setClearButtonEnabled(True)
        self.search.setFixedWidth(390)
        self.search.setFixedHeight(34)
        self.search.textChanged.connect(self._filter_tools)
        search_layout.addWidget(self.search)
        self.search_host.setGeometry((self.width() - 430) // 2, 4, 430, 34)
        self.search_host.show()
        self.search_host.raise_()
        shortcuts = (
            ("Ctrl+K", lambda: self.search.setFocus()),
            ("Ctrl+,", lambda: self.set_page("settings")),
            ("Ctrl+J", lambda: self._toggle_monitor(not self.console.isVisible())),
            ("Ctrl+L", lambda: self.console.output_search.setFocus()),
            ("Ctrl+R", lambda: self._rerun_task(self.console.task or {})),
        )
        for sequence, callback in shortcuts:
            action = QAction(self)
            action.setShortcut(QKeySequence(sequence))
            action.triggered.connect(callback)
            self.addAction(action)

    def _tray(self) -> None:
        self.tray = QSystemTrayIcon(self.windowIcon(), self)
        tray_menu = QMenu()
        show_action = tray_menu.addAction("显示 ARK CREW Studio")
        show_action.triggered.connect(self._show_window)
        self.tray_recent = tray_menu.addMenu("最近工作区")
        tray_menu.addSeparator()
        tray_menu.addAction("退出", self.close)
        self.tray.setContextMenu(tray_menu)
        self.tray.activated.connect(lambda reason: self._show_window() if reason == QSystemTrayIcon.ActivationReason.Trigger else None)
        self.tray.show()
        self._refresh_recent_menus()

    def _welcome_page(self) -> QWidget:
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(160, 80, 160, 80)
        layout.addStretch()
        logo = QLabel()
        logo.setAlignment(Qt.AlignmentFlag.AlignCenter)
        logo.setPixmap(logo_pixmap(132))
        title = QLabel("ARK CREW Studio")
        title.setObjectName("pageTitle")
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        copy = QLabel("选择 ARK CREW SDK 工作区，开始配置、构建、烧录和调试。")
        copy.setObjectName("pageDescription")
        copy.setAlignment(Qt.AlignmentFlag.AlignCenter)
        open_button = primary_button("选择 SDK 工作区", "folder")
        open_button.setMaximumWidth(220)
        open_button.clicked.connect(self.choose_workspace)
        layout.addWidget(logo)
        layout.addWidget(title)
        layout.addWidget(copy)
        layout.addSpacing(16)
        layout.addWidget(open_button, 0, Qt.AlignmentFlag.AlignCenter)
        layout.addStretch()
        return page

    def set_page(self, name: str) -> None:
        if name != "welcome" and not self.workspace.get("opened"):
            name = "welcome"
        page = self.page_map[name]
        animate = self.isVisible() and page is not self.pages.currentWidget()
        self.pages.show_page(page, animated=animate)
        if name == "workbench" and self.monitor_action.isChecked():
            self._set_tool_log_ratio()
        for key, button in self.nav_buttons.items():
            button.setProperty("active", key == name)
            button.style().unpolish(button)
            button.style().polish(button)
        if name == "artifacts":
            self._refresh_artifacts()

    def choose_workspace(self) -> None:
        start = str(self.workspace.get("path", "") or Path.cwd())
        selected = QFileDialog.getExistingDirectory(self, "选择 ARK CREW SDK 工作区", start)
        if selected:
            self.brain.request("workspace.open", {"path": selected}, self._workspace_loaded, self._error)

    def _workspace_loaded(self, workspace: Any) -> None:
        if not isinstance(workspace, dict):
            return
        self.workspace = workspace
        self.ui_state.remember_workspace(str(workspace.get("path", "")))
        self._refresh_recent_menus()
        self._watch_workspace()
        self._studio_log("success", "workspace", f"工作区已打开：{workspace.get('path', '')}")
        self.set_page("workbench")
        self._refresh_all()

    def _select_app(self, app: str) -> None:
        if app and app != self.workspace.get("activeApp"):
            self.brain.request("workspace.open", {"app": app}, self._workspace_loaded, self._error)

    def _filter_tools(self, text: str) -> None:
        self.workbench.set_tools(self.tools, self.sdk_check.get("tools", {}), text)

    def execute_tool(self, tool: dict[str, Any]) -> None:
        if tool.get("uiMode") == "serial_terminal":
            self.set_page("serial")
            return
        if self._task_running:
            self._error("已有工具正在运行，请等待当前任务结束")
            return
        if tool.get("parameterMode") == "none":
            self._confirm_and_start(tool, {})
            return
        def loaded(saved: Any) -> None:
            values = tool_defaults(tool, self.workspace)
            if isinstance(saved, dict):
                values.update(saved)
            self._show_tool_overlay(tool, values)

        self.brain.request("tool.params.get", {"toolId": tool.get("id")}, loaded, self._error)

    def detail_tool(self, tool: dict[str, Any]) -> None:
        if tool.get("uiMode") == "serial_terminal":
            self.set_page("serial")
            return
        if tool.get("parameterMode") == "none":
            self._show_tool_overlay(tool, {})
            return

        def loaded(saved: Any) -> None:
            values = tool_defaults(tool, self.workspace)
            if isinstance(saved, dict):
                values.update(saved)
            self._show_tool_overlay(tool, values)

        self.brain.request("tool.params.get", {"toolId": tool.get("id")}, loaded, self._error)

    def _confirm_and_start(self, tool: dict[str, Any], values: dict[str, Any]) -> None:
        if self._task_running:
            self._error("已有工具正在运行，请等待当前任务结束")
            return
        self._set_execution_locked(True)

        def failed(message: Any) -> None:
            self._set_execution_locked(False)
            self._error(message)

        self.brain.request("task.start", {"toolId": tool.get("id"), "params": values}, lambda task: self._focus_task(task), failed)

    def _show_tool_overlay(self, tool: dict[str, Any], values: dict[str, Any]) -> None:
        check = self.sdk_check.get("tools", {}).get(str(tool.get("id")), {})
        self.overlay.show_tool(
            tool,
            self.workspace,
            values,
            bool(check.get("available", False)),
            str(check.get("message", "等待SDK环境检查")),
            self._task_running,
        )

    def _start_from_overlay(self, tool: Any, values: Any) -> None:
        if isinstance(tool, dict) and isinstance(values, dict):
            self._confirm_and_start(tool, values)

    def _toggle_monitor(self, visible: bool) -> None:
        self.console.setVisible(visible)
        self.monitor_action.blockSignals(True)
        self.monitor_action.setChecked(visible)
        self.monitor_action.blockSignals(False)
        if visible:
            self._set_tool_log_ratio()

    def _toggle_global_logs(self, visible: bool) -> None:
        self.global_log_panel.setVisible(visible)
        if hasattr(self, "global_log_action"):
            self.global_log_action.blockSignals(True)
            self.global_log_action.setChecked(visible)
            self.global_log_action.blockSignals(False)
        self.status_bar.logs.setText("收起 Studio 日志" if visible else "展开 Studio 日志")

    def _rerun_task(self, task: Any) -> None:
        if not isinstance(task, dict) or not task:
            return
        tool_id = str(task.get("toolId", ""))
        tool = next((item for item in self.tools if str(item.get("id")) == tool_id), None)
        if tool is not None:
            values = dict(task.get("params", {})) if isinstance(task.get("params"), dict) else {}
            if tool.get("parameterMode") == "none" and not tool.get("confirmation"):
                self._confirm_and_start(tool, values)
            else:
                self._show_tool_overlay(tool, values or tool_defaults(tool, self.workspace))

    def _set_tool_log_ratio(self) -> None:
        height = max(self.main_splitter.height(), 500)
        self.main_splitter.setSizes([round(height * 0.6), round(height * 0.4)])

    def _open_project(self) -> None:
        value = str(self.workspace.get("cubeProjectPath") or self.workspace.get("projectPath") or "")
        path = Path(value)
        if path.is_file():
            path = path.parent
        if path.is_dir():
            QDesktopServices.openUrl(QUrl.fromLocalFile(str(path)))
        else:
            self._error("当前工程目录不存在")

    def _focus_task(self, task: Any) -> None:
        if isinstance(task, dict):
            self._set_execution_locked(task.get("state") in {"queued", "running"})
            self._upsert_task(task)
            self.console.set_task(task, bool(self.settings.get("log_timestamps", True)))
            self._toggle_monitor(True)
            self.set_page("workbench")

    def _event(self, name: str, data: Any) -> None:
        value = data if isinstance(data, dict) else {}
        if name == "system.ready":
            workspace = value.get("workspace")
            if isinstance(workspace, dict):
                self.workspace = workspace
                if workspace.get("opened"):
                    self.ui_state.remember_workspace(str(workspace.get("path", "")))
                    self._watch_workspace()
                    self.set_page("workbench")
            self.sdk_check = value.get("sdkCheck") if isinstance(value.get("sdkCheck"), dict) else {}
            self._studio_log("success", "brain", "Brain 初始化完成")
            self._refresh_all()
        elif name == "workspace.changed":
            self._workspace_loaded(value)
        elif name.startswith("sdk_check."):
            self.brain.request("sdk_check.snapshot", {}, self._set_sdk_check)
        elif name == "task.started":
            self._set_execution_locked(True)
            self._upsert_task(value)
            self.console.set_task(value, bool(self.settings.get("log_timestamps", True)))
            self._toggle_monitor(True)
        elif name == "task.progress":
            task = self._task_by_id.get(str(value.get("taskId")))
            if task:
                task.update({key: value.get(key) for key in ("state", "percent", "stage", "message")})
            self.console.update_progress(value)
        elif name == "task.log":
            task_id = str(value.get("taskId"))
            entry = value.get("entry") if isinstance(value.get("entry"), dict) else {}
            task = self._task_by_id.get(task_id)
            if task is not None:
                task.setdefault("logs", []).append(entry)
            self.console.append_log(task_id, entry)
        elif name == "task.finished":
            self._set_execution_locked(False)
            self._upsert_task(value)
            if self.console.task and self.console.task.get("id") == value.get("id"):
                self.console.set_task(value, bool(self.settings.get("log_timestamps", True)))
            self._task_finished_notification(value)
            self._refresh_artifacts()
        elif name == "task.history.cleared":
            self._refresh_tasks()
        elif name == "settings.updated":
            self.settings = value
            self.settings_page.set_values(self.workspace, self.settings)
        elif name == "serial.opened":
            self.serial_snapshot = value
            self.serial_page.set_snapshot(value)
        elif name in {"serial.rx", "serial.tx"}:
            self.serial_page.append_serial(name.split(".")[1], value)
        elif name == "serial.error":
            self.serial_page.append_error(str(value.get("message", "串口错误")))
        elif name == "serial.closed":
            self.serial_snapshot = value
            self.serial_page.set_snapshot(value)

    def _refresh_all(self) -> None:
        self.brain.request("workspace.snapshot", {}, self._set_workspace, self._error)
        self.brain.request("registry.list", {}, self._set_tools, self._error)
        self.brain.request("task.list", {}, self._set_tasks, self._error)
        self.brain.request("sdk_check.snapshot", {}, self._set_sdk_check, self._error)
        self.brain.request("settings.get", {}, self._set_settings, self._error)
        self.brain.request("serial.snapshot", {}, self.serial_page.set_snapshot, self._error)

    def _set_workspace(self, value: Any) -> None:
        if not isinstance(value, dict):
            return
        self.workspace = value
        self.global_log_panel.set_context(value)
        if value.get("opened"):
            self.workbench.set_workspace(value, self.sdk_check)
            self.settings_page.set_values(value, self.settings)
        else:
            self.set_page("welcome")

    def _set_tools(self, value: Any) -> None:
        self.tools = list(value) if isinstance(value, list) else []
        self.workbench.set_tools(self.tools, self.sdk_check.get("tools", {}), self.search.text())
        self.tool_menu.clear()
        for tool in self.tools:
            action = self.tool_menu.addAction(str(tool.get("title", tool.get("id", "工具"))))
            action.triggered.connect(lambda _checked=False, item=tool: self.detail_tool(item))
        self.tool_menu.addSeparator()
        self.tool_menu.addAction("SDK 环境检查", lambda: self.set_page("sdk-check"))

    def _set_tasks(self, value: Any) -> None:
        self.tasks = list(value) if isinstance(value, list) else []
        self._task_by_id = {str(item.get("id")): item for item in self.tasks}
        self._set_execution_locked(any(item.get("state") in {"queued", "running"} for item in self.tasks))
        self.recent.set_tasks(self.tasks, bool(self.settings.get("log_timestamps", True)))
        self.workbench.set_recent_tasks(self.tasks)
        if self.console.task is None and self.tasks:
            self.console.set_task(self.tasks[0], bool(self.settings.get("log_timestamps", True)))

    def _set_sdk_check(self, value: Any) -> None:
        self.sdk_check = value if isinstance(value, dict) else {}
        self.sdk_page.set_snapshot(self.sdk_check)
        self.workbench.set_workspace(self.workspace, self.sdk_check)
        self.workbench.set_tools(self.tools, self.sdk_check.get("tools", {}), self.search.text())

    def _set_settings(self, value: Any) -> None:
        self.settings = value if isinstance(value, dict) else {}
        self.settings_page.set_values(self.workspace, self.settings)
        self.recent.set_tasks(self.tasks, bool(self.settings.get("log_timestamps", True)))

    def _settings_saved(self, value: Any) -> None:
        self._set_settings(value)
        self._studio_log("success", "settings", "设置已保存")

    def _upsert_task(self, task: dict[str, Any]) -> None:
        task_id = str(task.get("id", ""))
        self._task_by_id[task_id] = task
        self.tasks = sorted(self._task_by_id.values(), key=lambda item: str(item.get("createdAt", "")), reverse=True)
        self.recent.set_tasks(self.tasks, bool(self.settings.get("log_timestamps", True)))
        self.workbench.set_recent_tasks(self.tasks)

    def _set_execution_locked(self, locked: bool) -> None:
        self._task_running = locked
        self.status_bar.set_running(locked)
        self.workbench.set_execution_locked(locked)
        self.overlay.set_execution_locked(locked)

    def _refresh_tasks(self) -> None:
        self.brain.request("task.list", {}, self._set_tasks, self._error)

    def _refresh_artifacts(self) -> None:
        if self.workspace.get("opened"):
            self.brain.request("artifact.list", {"app": self.workspace.get("activeApp")}, lambda value: self.artifacts.set_artifacts(list(value) if isinstance(value, list) else []), self._error)

    def _workspace_changed(self, _path: str) -> None:
        self.brain.request("sdk_check.start", {"force": True, "mode": "quick", "reloadRegistry": True, "reason": "工作区文件已变化"}, error=self._error)
        self.brain.request("registry.list", {}, self._set_tools, self._error)

    def _watch_workspace(self) -> None:
        old = self.file_watcher.directories() + self.file_watcher.files()
        if old:
            self.file_watcher.removePaths(old)
        root = Path(str(self.workspace.get("path", "")))
        active = str(self.workspace.get("activeApp", ""))
        paths = [root / "component" / "common", root / "hal" / "common", root / "app" / active]
        self.file_watcher.addPaths([str(path) for path in paths if path.exists()])

    def _refresh_recent_menus(self) -> None:
        for menu in (getattr(self, "recent_menu", None), getattr(self, "tray_recent", None)):
            if menu is None:
                continue
            menu.clear()
            recent = [str(item) for item in self.ui_state.values.get("recentWorkspaces", []) if Path(str(item)).is_dir()]
            if not recent:
                action = menu.addAction("暂无最近工作区")
                action.setEnabled(False)
            for path in recent:
                menu.addAction(path, lambda _checked=False, value=path: self.brain.request("workspace.open", {"path": value}, self._workspace_loaded, self._error))

    def _task_finished_notification(self, task: dict[str, Any]) -> None:
        if not self.isActiveWindow() and self.ui_state.values.get("notifyOnFinish", True):
            self.tray.showMessage(
                str(task.get("title", "任务")),
                f"{STATE_TEXT.get(str(task.get('state')), str(task.get('state')))}：{task.get('message', '')}",
                QSystemTrayIcon.MessageIcon.Information if task.get("state") == "succeeded" else QSystemTrayIcon.MessageIcon.Warning,
                5000,
            )

    def _brain_state(self, online: bool) -> None:
        if self._last_brain_online == online:
            return
        self._last_brain_online = online
        self.status_bar.set_brain(online)
        self._studio_log("success" if online else "error", "brain", "Brain 在线" if online else "Brain 离线")

    def _error(self, message: Any) -> None:
        text = safe_text(message)
        self._studio_log("error", "studio", text)

    def _studio_log(self, level: str, source: str, message: str) -> None:
        if not hasattr(self, "studio_log"):
            return
        entry = {
            "timestamp": datetime.now().astimezone().isoformat(),
            "level": level,
            "source": source,
            "text": safe_text(message),
        }
        self.global_log_panel.append_log(entry)
        if level == "warning":
            self._studio_warning_count += 1
        elif level == "error":
            self._studio_error_count += 1
            self._toggle_global_logs(True)
        self.status_bar.set_counts(self._studio_warning_count, self._studio_error_count)

    def _show_window(self) -> None:
        self.showNormal()
        self.raise_()
        self.activateWindow()

    def smoke_exit(self) -> None:
        _diagnostic("run:smoke-exit")
        self._closing = True
        self.tray.hide()
        self.brain.stop()
        self.close()
        QApplication.quit()

    def _restore_window(self) -> None:
        geometry = str(self.ui_state.values.get("geometry", ""))
        state = str(self.ui_state.values.get("windowState", ""))
        if geometry:
            self.restoreGeometry(QByteArray.fromBase64(geometry.encode("ascii")))
        if state:
            self.restoreState(QByteArray.fromBase64(state.encode("ascii")))

    def resizeEvent(self, event: Any) -> None:
        super().resizeEvent(event)
        if hasattr(self, "search_host"):
            self.search_host.setGeometry(
                (event.size().width() - 430) // 2,
                4,
                430,
                34,
            )
        if hasattr(self, "overlay"):
            self.overlay.setGeometry(self.centralWidget().rect())

    def closeEvent(self, event: QCloseEvent) -> None:
        active = [task for task in self.tasks if task.get("state") in {"queued", "running"}]
        if not self._closing and (active or self.serial_page.opened):
            answer = QMessageBox.question(
                self,
                "退出 ARK CREW Studio",
                "仍有活动任务或串口连接。退出会终止会话，是否继续？",
                QMessageBox.StandardButton.No | QMessageBox.StandardButton.Yes,
                QMessageBox.StandardButton.No,
            )
            if answer != QMessageBox.StandardButton.Yes:
                event.ignore()
                return
        self._closing = True
        self.ui_state.values["geometry"] = bytes(self.saveGeometry().toBase64()).decode("ascii")
        self.ui_state.values["windowState"] = bytes(self.saveState().toBase64()).decode("ascii")
        self.ui_state.values["mainSplitter"] = self.main_splitter.sizes()
        self.ui_state.save()
        self.tray.hide()
        self.brain.stop()
        event.accept()


def tool_defaults(tool: dict[str, Any], workspace: dict[str, Any]) -> dict[str, Any]:
    values: dict[str, Any] = {}
    for field in tool.get("inputs", []):
        name = str(field.get("name"))
        field_type = str(field.get("type"))
        value = field.get("default")
        if field_type == "probe":
            value = workspace.get("settings", {}).get("default_probe", "dap")
        elif field_type == "app":
            value = workspace.get("activeApp", "")
        elif isinstance(value, str):
            value = value.replace("${workspace}", str(workspace.get("path", "")))
            value = value.replace("${workspace_parent}", str(Path(str(workspace.get("path", "."))).parent))
            value = value.replace("${cube_project}", str(workspace.get("cubeProjectPath", "")))
            value = value.replace("${app_config}", str(workspace.get("configPath", "")))
            if field_type in {"file", "directory"}:
                value = os.path.normpath(value)
        elif value is None:
            value = False if field_type == "boolean" else ""
        values[name] = value
    return values


def run(workspace: Path | None = None) -> int:
    _diagnostic("run:start")
    QApplication.setHighDpiScaleFactorRoundingPolicy(Qt.HighDpiScaleFactorRoundingPolicy.PassThrough)
    application = QApplication(sys.argv)
    _diagnostic("run:application")
    application.setApplicationName("ARK CREW Studio")
    application.setApplicationVersion(__version__)
    application.setOrganizationName("ARK CREW")
    font_id = QFontDatabase.addApplicationFont(str(ASSET_ROOT / "fonts" / "NotoSansSC-VF.ttf"))
    families = QFontDatabase.applicationFontFamilies(font_id) if font_id >= 0 else []
    application_font = QFont()
    if os.name == "nt":
        application_font.setFamilies(["Segoe UI", "Microsoft YaHei UI", "Noto Sans SC"])
    else:
        application_font.setFamilies([families[0] if families else "Noto Sans SC"])
    application_font.setPointSizeF(10.0)
    application_font.setHintingPreference(QFont.HintingPreference.PreferFullHinting)
    application_font.setStyleStrategy(QFont.StyleStrategy.PreferAntialias)
    application.setFont(application_font)
    _diagnostic(f"run:font:{QFontInfo(application.font()).family()}")
    application.setWindowIcon(app_icon())
    application.setQuitOnLastWindowClosed(True)
    application.setStyle(ArkStyle("Fusion"))
    application.setStyleSheet(STYLESHEET)
    window = MainWindow(workspace)
    _diagnostic("run:window")
    window.show()
    _diagnostic("run:shown")
    smoke_exit = int(os.environ.get("ARK_STUDIO_SMOKE_EXIT_MS", "0") or 0)
    if smoke_exit > 0:
        QTimer.singleShot(smoke_exit, window.smoke_exit)
        _diagnostic(f"run:smoke:{smoke_exit}")
    _diagnostic("run:exec")
    return application.exec()


def _diagnostic(message: str) -> None:
    path = os.environ.get("ARK_STUDIO_DIAGNOSTIC_LOG", "").strip()
    if path:
        with Path(path).open("a", encoding="utf-8") as stream:
            stream.write(message + "\n")
