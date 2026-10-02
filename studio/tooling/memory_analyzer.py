#!/usr/bin/env python3
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

"""Analyze Keil map usage by SDK layer and show a compact GUI report."""

from __future__ import annotations

import argparse
from collections import defaultdict
from dataclasses import dataclass
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

ctk = None
messagebox = None
ttk = None

from studio.tooling.project_config import load_app_config, resolve_keil_device_profile, resolve_selection


@dataclass
class MemoryItem:
    name: str
    category: str
    code: int
    ro: int
    rw: int
    zi: int
    source: str = ""

    @property
    def flash(self) -> int:
        return self.code + self.ro + self.rw

    @property
    def sram(self) -> int:
        return self.rw + self.zi


@dataclass
class TaskItem:
    name: str
    stack_words: int
    owner: str
    source: str
    lifetime: str = "常驻"

    @property
    def stack_bytes(self) -> int:
        return self.stack_words * 4


def _target_node(project: Path, target: str) -> ET.Element:
    for node in ET.parse(project).getroot().findall(".//Target"):
        if node.findtext("TargetName") == target:
            return node
    raise ValueError(f"Keil target not found: {target}")


def _map_path(project: Path, target: str) -> Path:
    node = _target_node(project, target)
    output = (node.findtext("./TargetOption/TargetCommonOption/OutputDirectory") or "").strip()
    output_name = (node.findtext("./TargetOption/TargetCommonOption/OutputName") or target).strip()
    path = project.parent / output / f"{output_name}.map"
    if not path.is_file():
        raise ValueError(f"Keil map file not found; build the project first: {path}")
    return path.resolve()


def _project_sources(project: Path, target: str) -> dict[str, tuple[str, str]]:
    mapping: dict[str, tuple[str, str]] = {}
    target_node = _target_node(project, target)
    for group in target_node.findall(".//Group"):
        group_name = group.findtext("GroupName") or ""
        for file_node in group.findall("./Files/File"):
            path = (file_node.findtext("FilePath") or "").replace("\\", "/")
            stem = Path(file_node.findtext("FileName") or path).stem.lower() + ".o"
            mapping[stem] = (group_name, path)
    return mapping


def _classify(name: str, group: str, source: str, components: tuple[str, ...]) -> str:
    normalized = source.replace("\\", "/").lower()
    for component in components:
        if f"/component/{component.lower()}/" in normalized:
            return f"组件/{component}"
    if "/component/common/" in normalized:
        return "组件/公共总线"
    if "/ark_sdk/hal/" in normalized:
        return "HAL/SDK适配层"
    if "/ark_sdk/app/" in normalized:
        return "App"
    if "freertos" in group.lower() or any(
        token in name for token in (
            "tasks.o", "queue.o", "list.o", "timers.o", "stream_buffer.o",
            "event_groups.o", "croutine.o", "heap_4.o", "port.o", "cmsis_os2.o",
        )
    ):
        return "OS/FreeRTOS"
    if "stm32f1xx_hal" in name or "drivers/stm32f1xx_hal" in normalized:
        return "厂商HAL库"
    if group.startswith("Application/User"):
        return "CubeMX生成代码"
    if "startup" in name or "cmsis" in group.lower() or "system_stm32" in name:
        return "启动与CMSIS"
    if not source:
        return "运行库与其它"
    return "其它工程代码"


def parse_map(config_path: Path) -> tuple[list[MemoryItem], dict[str, int], Path]:
    config = load_app_config(config_path)
    map_path = _map_path(config.keil_project, config.target)
    sources = _project_sources(config.keil_project, config.target)
    text = map_path.read_text(encoding="utf-8", errors="replace")
    start = text.find("Image component sizes")
    end = text.find("Grand Totals", start)
    if start < 0 or end < 0:
        raise ValueError(f"Image component size table not found: {map_path}")
    table = text[start:end]
    row = re.compile(
        r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+([^\s]+\.o)\s*$",
        re.MULTILINE,
    )
    items: list[MemoryItem] = []
    seen_object_section_end = table.find("Object Totals")
    for match in row.finditer(table):
        name = match.group(7)
        is_project_object = match.start() < seen_object_section_end
        group, source = sources.get(name.lower(), ("", "")) if is_project_object else ("", "")
        category = _classify(name.lower(), group, source, config.components)
        items.append(MemoryItem(
            name=name,
            category=category,
            code=int(match.group(1)),
            ro=int(match.group(3)),
            rw=int(match.group(4)),
            zi=int(match.group(5)),
            source=source,
        ))
    totals_match = re.search(
        r"^\s*(\d+)\s+\d+\s+(\d+)\s+(\d+)\s+(\d+)\s+\d+\s+Grand Totals\s*$",
        text,
        re.MULTILINE,
    )
    if not items or totals_match is None:
        raise ValueError(f"Incomplete memory data in map file: {map_path}")
    totals = {
        "code": int(totals_match.group(1)),
        "ro": int(totals_match.group(2)),
        "rw": int(totals_match.group(3)),
        "zi": int(totals_match.group(4)),
    }
    totals["flash"] = totals["code"] + totals["ro"] + totals["rw"]
    totals["sram"] = totals["rw"] + totals["zi"]
    return items, totals, map_path


def _owner_for_source(source: Path, config) -> str:
    normalized = source.as_posix().lower()
    for component in config.components:
        if f"/component/{component.lower()}/" in normalized:
            return f"组件/{component}"
    if "/component/common/" in normalized:
        return "组件/公共总线"
    if "/hal/" in normalized:
        return "HAL"
    if "/app/" in normalized:
        return "App"
    return "其它"


def parse_tasks(config_path: Path) -> tuple[list[TaskItem], int]:
    config = load_app_config(config_path)
    selection = resolve_selection(config)
    sdk_root = Path(os.environ.get("ARK_SDK_ROOT", Path(__file__).resolve().parents[2])).resolve()
    sources: list[Path] = []
    for group_sources in selection.groups.values():
        for value in group_sources:
            if value.startswith("@project/"):
                path = config.keil_project.parent.parent / value.removeprefix("@project/")
            else:
                path = sdk_root / value
            if path.suffix.lower() == ".c" and path.is_file():
                sources.append(path.resolve())
    for _name, (_group, value) in _project_sources(config.keil_project, config.target).items():
        path = (config.keil_project.parent / value).resolve()
        if path.suffix.lower() == ".c" and path.is_file():
            sources.append(path)
    tasks: list[TaskItem] = []
    for source in sorted(set(sources)):
        text = source.read_text(encoding="utf-8", errors="replace")
        macros = {
            name: int(value)
            for name, value in re.findall(
                r"^\s*#define\s+([A-Za-z_]\w*STACK(?:_DEPTH)?)\s+(\d+)[UuLl]*\s*$",
                text,
                re.MULTILINE,
            )
        }
        for match in re.finditer(
            r"xTaskCreate\s*\(\s*[^,]+,\s*\"([^\"]+)\"\s*,\s*([A-Za-z_]\w*|\d+[UuLl]*)",
            text,
            re.DOTALL,
        ):
            token = match.group(2).rstrip("UuLl")
            words = int(token) if token.isdigit() else macros.get(match.group(2))
            if words is not None:
                task_name = match.group(1)
                lifetime = "自检临时" if task_name == "componentTest" else "常驻"
                tasks.append(TaskItem(task_name, words, _owner_for_source(source, config), str(source), lifetime))
        attributes = {
            name: int(size)
            for name, size in re.findall(
                r"const\s+osThreadAttr_t\s+([A-Za-z_]\w*)\s*=\s*\{.*?\.stack_size\s*=\s*(\d+)\s*\*\s*4.*?\};",
                text,
                re.DOTALL,
            )
        }
        for match in re.finditer(
            r"osThreadNew\s*\(\s*[^,]+,\s*[^,]+,\s*&([A-Za-z_]\w*)\s*\)", text
        ):
            words = attributes.get(match.group(1))
            if words is not None:
                tasks.append(TaskItem(
                    match.group(1).removesuffix("_attributes"), words,
                    "CubeMX启动任务", str(source), "启动后删除",
                ))
    freertos_config = config.keil_project.parent.parent / "Core" / "Inc" / "FreeRTOSConfig.h"
    heap = 0
    if freertos_config.is_file():
        match = re.search(r"#define\s+configTOTAL_HEAP_SIZE\s+\(.*?(\d+)\s*\)",
                          freertos_config.read_text(encoding="utf-8", errors="replace"))
        if match:
            heap = int(match.group(1))
    return tasks, heap


def analyze(config_path: Path) -> dict:
    config = load_app_config(config_path)
    profile = resolve_keil_device_profile(config.device)
    items, totals, map_path = parse_map(config_path)
    tasks, heap = parse_tasks(config_path)
    categories: dict[str, dict[str, int]] = defaultdict(lambda: {"flash": 0, "sram": 0})
    for item in items:
        categories[item.category]["flash"] += item.flash
        categories[item.category]["sram"] += item.sram
    flash_total = int(re.search(r"IROM\([^,]+,0x([0-9A-Fa-f]+)\)", profile.cpu).group(1), 16)
    sram_total = int(re.search(r"IRAM\([^,]+,0x([0-9A-Fa-f]+)\)", profile.cpu).group(1), 16)
    return {
        "app": config.app,
        "map": str(map_path),
        "items": items,
        "categories": dict(categories),
        "tasks": tasks,
        "heap": heap,
        "flash_used": totals["flash"],
        "flash_total": flash_total,
        "sram_used": totals["sram"],
        "sram_total": sram_total,
    }


def _fmt(value: int) -> str:
    return f"{value:,} B"


def print_report(report: dict) -> None:
    print(f"Memory report: {report['app']}")
    print(f"Map: {report['map']}")
    print(f"Flash: {_fmt(report['flash_used'])} / {_fmt(report['flash_total'])} "
          f"({report['flash_used'] * 100.0 / report['flash_total']:.2f}%)")
    print(f"SRAM : {_fmt(report['sram_used'])} / {_fmt(report['sram_total'])} "
          f"({report['sram_used'] * 100.0 / report['sram_total']:.2f}%)")
    print("\nCategory                         Flash       SRAM")
    for name, values in sorted(report["categories"].items(), key=lambda pair: -pair[1]["flash"]):
        print(f"{name:<30} {_fmt(values['flash']):>12} {_fmt(values['sram']):>12}")
    print(f"\nFreeRTOS heap pool (included in static SRAM): {_fmt(report['heap'])}")
    print("Configured dynamic task stacks (budget, not extra link-time SRAM):")
    for task in report["tasks"]:
        print(f"  {task.name:<18} {task.owner:<20} {task.stack_words:>4} words  "
              f"{_fmt(task.stack_bytes):>10}  {task.lifetime}")


class MemoryWindow:
    def __init__(self, root, report: dict) -> None:
        self.root = root
        root.title(f"ARK CREW SDK 内存分析 - {report['app']}")
        root.geometry("1180x760")
        root.minsize(980, 640)
        root.columnconfigure(0, weight=1)
        root.rowconfigure(2, weight=1)

        title = ctk.CTkFrame(root, fg_color="transparent")
        title.grid(row=0, column=0, padx=20, pady=(18, 8), sticky="ew")
        title.columnconfigure((0, 1), weight=1)
        ctk.CTkLabel(title, text=f"{report['app']} · 编译内存分析",
                     font=ctk.CTkFont(size=24, weight="bold")).grid(row=0, column=0, sticky="w")
        ctk.CTkLabel(title, text=report["map"], anchor="e").grid(row=0, column=1, sticky="e")

        cards = ctk.CTkFrame(root)
        cards.grid(row=1, column=0, padx=20, pady=8, sticky="ew")
        cards.columnconfigure((0, 1, 2), weight=1)
        self._card(cards, 0, "Flash", report["flash_used"], report["flash_total"])
        self._card(cards, 1, "静态SRAM", report["sram_used"], report["sram_total"])
        task_bytes = sum(task.stack_bytes for task in report["tasks"])
        self._card(cards, 2, "已声明任务栈合计 / FreeRTOS Heap", task_bytes, report["heap"])

        tabs = ctk.CTkTabview(root)
        tabs.grid(row=2, column=0, padx=20, pady=(8, 18), sticky="nsew")
        for name in ("分类总览", "目标文件", "任务栈"):
            tabs.add(name)
            tabs.tab(name).columnconfigure(0, weight=1)
            tabs.tab(name).rowconfigure(0, weight=1)
        categories = [
            (name, values["flash"], values["sram"])
            for name, values in sorted(report["categories"].items(), key=lambda pair: -pair[1]["flash"])
        ]
        self._table(tabs.tab("分类总览"), ("类别", "Flash", "SRAM", "Flash占比", "SRAM占比"), [
            (name, _fmt(flash), _fmt(sram),
             f"{flash * 100.0 / report['flash_used']:.1f}%",
             f"{sram * 100.0 / report['sram_used']:.1f}%")
            for name, flash, sram in categories
        ])
        self._table(tabs.tab("目标文件"), ("目标文件", "归属", "Flash", "SRAM", "Code", "RO", "RW", "ZI", "源文件"), [
            (item.name, item.category, _fmt(item.flash), _fmt(item.sram),
             str(item.code), str(item.ro), str(item.rw), str(item.zi), item.source or "Keil运行库")
            for item in sorted(report["items"], key=lambda value: -(value.flash + value.sram))
        ])
        self._table(tabs.tab("任务栈"), ("任务", "归属", "栈Words", "栈Bytes", "生命周期", "说明"), [
            (task.name, task.owner, str(task.stack_words), _fmt(task.stack_bytes),
             task.lifetime, "动态分配，来自FreeRTOS Heap") for task in report["tasks"]
        ])
        ctk.CTkLabel(
            tabs.tab("任务栈"),
            text="说明：任务栈来自FreeRTOS Heap；启动任务、自检临时任务与运行任务并非全部同时存在，因此合计值不是峰值。",
            anchor="w",
        ).grid(row=1, column=0, padx=4, pady=6, sticky="ew")

    def _card(self, parent, column: int, label: str, used: int, total: int) -> None:
        frame = ctk.CTkFrame(parent)
        frame.grid(row=0, column=column, padx=8, pady=10, sticky="ew")
        ctk.CTkLabel(frame, text=label, font=ctk.CTkFont(size=15, weight="bold")).pack(pady=(10, 2))
        percent = (used * 100.0 / total) if total else 0.0
        ctk.CTkLabel(frame, text=f"{_fmt(used)} / {_fmt(total)}   {percent:.2f}%").pack()
        bar = ctk.CTkProgressBar(frame)
        bar.pack(fill="x", padx=16, pady=(6, 12))
        bar.set(min(percent / 100.0, 1.0))

    def _table(self, parent, columns: tuple[str, ...], rows: list[tuple]) -> None:
        style = ttk.Style()
        style.configure("Treeview", rowheight=26)
        tree = ttk.Treeview(parent, columns=columns, show="headings")
        scrollbar = ttk.Scrollbar(parent, orient="vertical", command=tree.yview)
        tree.configure(yscrollcommand=scrollbar.set)
        tree.grid(row=0, column=0, sticky="nsew")
        scrollbar.grid(row=0, column=1, sticky="ns")
        for column in columns:
            tree.heading(column, text=column)
            width = 300 if column == "源文件" else (150 if column in ("类别", "归属") else 105)
            tree.column(column, width=width, anchor="w")
        for row in rows:
            tree.insert("", "end", values=row)


def main() -> int:
    global ctk, messagebox, ttk
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config", type=Path, required=True,
        help="Path to app/<name>/<name>.dts")
    parser.add_argument("--text", action="store_true", help="Print report instead of opening GUI")
    parser.add_argument("--json", action="store_true", help="Print machine-readable summary")
    parser.add_argument("--check", action="store_true", help="Check that a current MAP report can be analyzed")
    args = parser.parse_args()
    try:
        print("[ARK_PROGRESS] 10 检查Keil MAP与工程配置", flush=True)
        report = analyze(args.config.resolve())
        print("[ARK_PROGRESS] 75 内存分类与任务栈统计完成", flush=True)
        if args.check:
            print(f"memory report ready: {report['map']}")
            print("[ARK_PROGRESS] 100 内存分析环境检查通过", flush=True)
            return 0
        if args.json:
            serializable = {key: value for key, value in report.items() if key not in ("items", "tasks")}
            serializable["items"] = [item.__dict__ | {"flash": item.flash, "sram": item.sram} for item in report["items"]]
            serializable["tasks"] = [task.__dict__ | {"stack_bytes": task.stack_bytes} for task in report["tasks"]]
            print(json.dumps(serializable, ensure_ascii=False, indent=2))
            print("[ARK_PROGRESS] 100 内存分析完成", flush=True)
            return 0
        if args.text:
            print_report(report)
            print("[ARK_PROGRESS] 100 内存分析完成", flush=True)
            return 0
        try:
            import customtkinter as ctk_module
            from tkinter import messagebox as messagebox_module, ttk as ttk_module
        except ImportError as exc:
            raise ValueError("CustomTkinter is required: python -m pip install customtkinter") from exc
        ctk = ctk_module
        messagebox = messagebox_module
        ttk = ttk_module
        ctk.set_appearance_mode("System")
        ctk.set_default_color_theme("dark-blue")
        root = ctk.CTk()
        MemoryWindow(root, report)
        root.mainloop()
        return 0
    except (OSError, ValueError, ET.ParseError) as exc:
        if not args.text and ctk is not None and messagebox is not None:
            root = ctk.CTk()
            root.withdraw()
            messagebox.showerror("内存分析失败", str(exc))
            root.destroy()
        else:
            print(f"memory analysis failed: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
