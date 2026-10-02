# SPDX-FileCopyrightText: Copyright (c) 2026 Ark Crew. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Ark-Crew-NonCommercial-1.0

"""Visual tokens for the ARK CREW Studio desktop workbench."""

STYLESHEET = r"""
* { font-size: 10.5pt; color: #f3f8fc; }
QMainWindow, QWidget#root { background: #07111d; }
QWidget#mainContent, QWidget[studioPage="true"] { background: #07111d; }
QMenuBar { background: #091522; border-bottom: 1px solid #244158; padding: 2px 10px; }
QMenuBar::item { padding: 6px 10px; margin: 3px 2px; border-radius: 6px; background: transparent; font-weight: 600; }
QMenuBar::item:selected, QMenu::item:selected { background: #173047; color: #ffffff; }
QMenu { background: #0c1b2a; border: 1px solid #31516a; padding: 5px; }
QMenu::item { padding: 7px 28px 7px 10px; border-radius: 5px; }
QLineEdit, QComboBox, QSpinBox { min-height: 36px; border: 1px solid #31516a; border-radius: 6px; padding: 0 10px; background: #081827; selection-background-color: #247eb2; }
QWidget#searchHost { background: transparent; }
QLineEdit#toolSearch { min-height: 34px; max-height: 34px; padding: 0 12px; }
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QPlainTextEdit:focus, QTableWidget:focus { border-color: #f3f8fc; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox QAbstractItemView { background: #0c1b2a; border: 1px solid #31516a; selection-background-color: #173b59; }
QPushButton { min-height: 36px; border: 1px solid #31516a; border-radius: 6px; padding: 0 12px; background: #102438; color: #d5e2eb; font-weight: 500; }
QPushButton:hover { border-color: #4d7896; background: #173047; color: #ffffff; }
QPushButton:focus { border-color: #f3f8fc; }
QPushButton:pressed { background: #091726; }
QPushButton:disabled { color: #61788a; border-color: #20394d; background: #0a1724; }
QPushButton[primary="true"] { color: #03131f; border-color: #55cef5; background: #42c7f4; font-weight: 600; }
QPushButton[primary="true"]:hover { background: #72d8f7; }
QPushButton[danger="true"] { border-color: #8d4655; color: #ff9aaa; background: #281720; }
QPushButton[segmented="true"] { min-height: 32px; max-height: 32px; padding: 0 10px; background: transparent; }
QPushButton[segmented="true"][active="true"] { background: #173047; border-color: #42c7f4; color: #ffffff; }
QPushButton[statusAction="true"] { min-height: 26px; max-height: 26px; border: none; background: transparent; padding: 0 6px; color: #a9bed0; }
QFrame#sidebar { background: #091522; border-right: 1px solid #244158; }
QPushButton[nav="true"] { min-height: 44px; border: none; border-left: 1px solid transparent; border-radius: 6px; text-align: left; padding-left: 13px; background: transparent; font-size: 10.5pt; font-weight: 600; }
QPushButton[nav="true"]:hover { background: #102438; border-left-color: #496b84; }
QPushButton[nav="true"][active="true"] { background: #173047; border-left-color: #42c7f4; color: #ffffff; }
QLabel#pageTitle { font-size: 18pt; font-weight: 700; color: #f3f8fc; }
QLabel#pageDescription { color: #a9bed0; font-size: 10pt; }
QLabel#sectionTitle { font-size: 13.5pt; font-weight: 700; color: #f3f8fc; }
QLabel#muted { color: #a9bed0; font-size: 9.5pt; }
QLabel#mono { color: #a9bed0; font-size: 9.5pt; }
QFrame#statusStrip, QFrame#panel, QFrame#toolCard, QFrame#console, QFrame#metric, QFrame#globalLogPanel { background: #0c1b2a; border: 1px solid #244158; border-radius: 7px; }
QFrame#toolCard:hover { border-color: #426f8d; background: #102438; }
QFrame#toolFilters { background: #091827; border: 1px solid #244158; border-radius: 7px; }
QLabel#toolTitle { font-size: 12pt; font-weight: 700; color: #f3f8fc; }
QLabel#toolDescription { color: #c2d1dc; font-size: 10pt; }
QLabel#tag { border: 1px solid #31516a; border-radius: 9px; padding: 2px 9px; color: #8fcde8; font-size: 9pt; font-weight: 600; }
QLabel#availabilityBadge { border: 1px solid #315a78; border-radius: 10px; padding: 3px 10px; font-size: 9.5pt; }
QLabel#availabilityBadge[status="success"] { color: #9fc5d8; border-color: #315a78; background: #102438; }
QLabel#availabilityBadge[status="warning"] { color: #f2b84b; border-color: #78602b; background: #2a2415; }
QLabel#taskStateBadge { min-height: 25px; max-height: 25px; border: 1px solid #315a78; border-radius: 10px; color: #a9bed0; font-size: 9.5pt; font-weight: 600; }
QLabel#taskStateBadge[status="success"] { color: #38d39f; border-color: #24785f; background: #102b25; }
QLabel#taskStateBadge[status="warning"] { color: #f2b84b; border-color: #78602b; background: #2a2415; }
QLabel#taskStateBadge[status="error"] { color: #ff6b7a; border-color: #8d4655; background: #281720; }
QLabel[status="success"] { color: #38d39f; }
QLabel[status="warning"] { color: #f2b84b; }
QLabel[status="error"] { color: #ff6b7a; }
QLabel#taskSummary { color: #d5e2eb; font-size: 10.5pt; }
QTableWidget, QListWidget, QTreeWidget, QTextEdit, QPlainTextEdit { background: #07131f; border: 1px solid #244158; border-radius: 6px; alternate-background-color: #0a1928; gridline-color: #1d394e; }
QHeaderView { background: #07131f; }
QHeaderView::section { min-height: 34px; background: #102438; border: none; border-bottom: 1px solid #31516a; padding: 0 10px; color: #a9bed0; font-size: 9.5pt; }
QTableCornerButton::section { background: #102438; border: none; border-bottom: 1px solid #31516a; }
QTableWidget::item, QListWidget::item, QTreeWidget::item { padding: 5px; }
QTableWidget::item:selected, QListWidget::item:selected, QTreeWidget::item:selected { background: #173b59; color: #ffffff; }
QTextEdit, QPlainTextEdit { font-size: 9.5pt; padding: 8px; selection-background-color: #245d83; }
QPlainTextEdit#studioLog { border-radius: 6px; background: #050d17; padding: 6px 10px; }
QPlainTextEdit[terminalActive="true"] { border-color: #42c7f4; background: #050e1a; }
QScrollArea, QScrollArea > QWidget > QWidget { background: #07111d; border: none; }
QScrollBar:vertical { width: 10px; background: transparent; margin: 2px; }
QScrollBar::handle:vertical { min-height: 30px; border-radius: 4px; background: #31516a; }
QScrollBar::handle:vertical:hover { background: #426f8d; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QSplitter::handle { background: #244158; }
QSplitter::handle:vertical { height: 4px; }
QSplitter::handle:horizontal { width: 4px; }
QProgressBar { min-height: 14px; max-height: 14px; border: 1px solid #365273; border-radius: 7px; background: #050b14; text-align: center; color: transparent; }
QProgressBar::chunk { border-radius: 6px; background: #42c7f4; }
QCheckBox { spacing: 8px; }
QCheckBox::indicator { width: 17px; height: 17px; }
QTabWidget::pane { border: 1px solid #244158; border-radius: 6px; top: -1px; background: #07131f; }
QTabBar::tab { min-height: 34px; padding: 0 16px; color: #a9bed0; background: transparent; border-bottom: 2px solid transparent; }
QTabBar::tab:hover { color: #f3f8fc; background: #102438; }
QTabBar::tab:selected { color: #f3f8fc; border-bottom-color: #42c7f4; font-weight: 600; }
QToolTip { background: #102438; color: #f3f8fc; border: 1px solid #426f8d; padding: 5px; }
QWidget#toolOverlay { background: transparent; }
QFrame#overlayPanel { background: #0c1b2a; border: 1px solid #426f8d; border-radius: 8px; }
QLabel#overlayTitle { font-size: 18pt; font-weight: 700; color: #f3f8fc; }
QLabel#overlayNotice { padding: 10px 12px; border-radius: 6px; background: #2a2415; color: #f2b84b; }
QPushButton#iconButton { min-width: 36px; max-width: 36px; min-height: 36px; max-height: 36px; padding: 0; }
QPushButton#taskAction { min-width: 88px; max-width: 88px; }
QPushButton#taskAction[taskState="queued"], QPushButton#taskAction[taskState="running"] { color: #ff9aaa; border-color: #8d4655; background: #281720; }
QPushButton#taskAction[taskState="succeeded"]:disabled { color: #07111d; border-color: #38d39f; background: #38d39f; }
QPushButton#taskAction[taskState="failed"]:disabled, QPushButton#taskAction[taskState="blocked"]:disabled { color: #ffafba; border-color: #8d4655; background: #281720; }
QPushButton#taskAction[taskState="cancelled"]:disabled, QPushButton#taskAction[taskState="idle"]:disabled { color: #8298aa; border-color: #244158; background: #0b1928; }
QFrame#studioStatusBar { background: #091522; border-top: 1px solid #244158; border-radius: 0; }
"""
