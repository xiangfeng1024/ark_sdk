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

"""Clone CubeMX projects safely and audit an STM32F103 USB CDC configuration."""

from __future__ import annotations

import argparse
from contextlib import redirect_stdout
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from studio.tooling.project_config import load_app_config
from studio.tooling.process_runner import hidden_process_kwargs


SDK_ROOT = Path(os.environ.get("ARK_SDK_ROOT", Path(__file__).resolve().parents[2])).resolve()
PROJECT_NAME_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]*$")
BUILD_SUFFIXES = {
    ".axf", ".bin", ".crf", ".d", ".dep", ".elf", ".hex", ".htm",
    ".iex", ".lnp", ".map", ".o", ".obj", ".sct", ".tra",
}


def _emit_progress(percent: int, message: str) -> None:
    print(f"[ARK_PROGRESS] {percent} {message}", flush=True)


def _cube_ioc(project: Path) -> Path:
    files = sorted(project.glob("*.ioc"))
    if len(files) != 1:
        raise ValueError(
            f"expected exactly one .ioc in {project}, found {len(files)}"
        )
    return files[0]


def _keil_project(project: Path, preferred_name: str | None = None) -> Path:
    directory = project / "MDK-ARM"
    files = sorted(directory.glob("*.uvprojx"))
    if preferred_name is not None:
        preferred = directory / f"{preferred_name}.uvprojx"
        if preferred.is_file():
            return preferred
    if len(files) != 1:
        raise ValueError(
            f"expected exactly one .uvprojx in {directory}, found {len(files)}"
        )
    return files[0]


def _clone_ignore(source_name: str):
    def ignore(directory: str, names: list[str]) -> set[str]:
        path = Path(directory)
        ignored: set[str] = set()

        for name in names:
            item = path / name
            lower = name.lower()
            if lower in {".keil", "debugconfig", "__pycache__"}:
                ignored.add(name)
            elif name == "ark_sdk":
                ignored.add(name)
            elif (path.name == "MDK-ARM") and (name == source_name):
                ignored.add(name)
            elif ".uvguix." in lower:
                ignored.add(name)
            elif item.is_file() and item.suffix.lower() in BUILD_SUFFIXES:
                ignored.add(name)
            elif lower.endswith(".build_log.htm"):
                ignored.add(name)
        return ignored

    return ignore


def _replace_project_name(path: Path, old_name: str, new_name: str) -> None:
    text = path.read_text(encoding="utf-8")
    updated = text.replace(old_name, new_name)
    if updated != text:
        path.write_text(updated, encoding="utf-8", newline="")


def _rename_project_files(project: Path, old_name: str, new_name: str) -> Path:
    old_ioc = _cube_ioc(project)
    new_ioc = project / f"{new_name}.ioc"
    old_ioc.rename(new_ioc)
    _replace_project_name(new_ioc, old_name, new_name)

    mdk = project / "MDK-ARM"
    for suffix in (".uvprojx", ".uvoptx"):
        old_path = mdk / f"{old_name}{suffix}"
        if old_path.is_file():
            new_path = mdk / f"{new_name}{suffix}"
            old_path.rename(new_path)
            _replace_project_name(new_path, old_name, new_name)

    for path in mdk.glob(f"{old_name}.uvguix.*"):
        path.unlink()

    project_file = _keil_project(project, new_name)
    _replace_project_name(project_file, old_name, new_name)
    return project_file


def _create_sdk_link(project: Path, sdk_root: Path) -> Path:
    link = project / "ark_sdk"
    if link.exists() or link.is_symlink():
        raise ValueError(f"SDK link target already exists: {link}")
    sdk_root = sdk_root.resolve()
    if not sdk_root.is_dir():
        raise ValueError(f"SDK root does not exist: {sdk_root}")

    if os.name == "nt":
        result = subprocess.run(
            ["cmd.exe", "/d", "/c", "mklink", "/J", str(link), str(sdk_root)],
            capture_output=True,
            text=True,
            check=False,
            **hidden_process_kwargs(),
        )
        if result.returncode != 0:
            message = (result.stderr or result.stdout).strip()
            raise OSError(f"failed to create SDK junction: {message}")
    else:
        link.symlink_to(sdk_root, target_is_directory=True)
    return link


def _portable_relative(target: Path, base: Path) -> str:
    return Path(os.path.relpath(target.resolve(), base.resolve())).as_posix()


def _rewrite_keil_sdk_path(project_file: Path, sdk_reference: str) -> None:
    """Replace legacy project-root ark_sdk references with a direct relative path."""
    text = project_file.read_text(encoding="utf-8")
    reference_from_mdk = (Path("..") / Path(sdk_reference)).as_posix()
    pattern = re.compile(r"(?<![A-Za-z0-9_.-])(?:\.\.[/\\])+ark_sdk(?=[/\\])")
    updated = pattern.sub(reference_from_mdk, text)
    if updated != text:
        project_file.write_text(updated, encoding="utf-8", newline="")


def clone_project(args: argparse.Namespace) -> int:
    _emit_progress(5, "检查克隆参数")
    source = args.source.resolve()
    destination = args.destination.resolve()
    sdk_root = args.sdk_root.resolve()

    if not source.is_dir():
        raise ValueError(f"source project does not exist: {source}")
    _emit_progress(15, "检查CubeMX与Keil工程结构")
    source_ioc = _cube_ioc(source)
    old_name = source_ioc.stem
    new_name = args.name or destination.name
    if not PROJECT_NAME_PATTERN.fullmatch(new_name):
        raise ValueError("project name may contain only letters, digits, '.', '_' and '-'")
    if "_" not in new_name:
        raise ValueError(
            "project name must use the unified <board_prefix>_<product> format")
    _keil_project(source, old_name)
    if destination == source or source in destination.parents:
        raise ValueError("destination must not be the source or a child of the source")
    if destination.exists():
        raise ValueError(f"destination already exists: {destination}")
    use_junction = bool(getattr(args, "sdk_junction", False))
    sdk_reference = "ark_sdk" if use_junction else _portable_relative(
        sdk_root, destination
    )
    summary = {
        "source": str(source),
        "destination": str(destination),
        "old_name": old_name,
        "new_name": new_name,
        "sdk_root": str(sdk_root),
        "sdk_mode": "junction" if use_junction else "relative",
        "sdk_reference": sdk_reference,
    }
    _emit_progress(30, "克隆路径与新工程名检查完成")
    if args.dry_run or getattr(args, "check", False):
        print(json.dumps(summary, ensure_ascii=False, indent=2))
        _emit_progress(100, "克隆环境检查通过")
        return 0

    _emit_progress(40, "创建安全临时目录")
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(
        prefix=f".{destination.name}.clone-",
        dir=destination.parent,
    ))
    temporary.rmdir()
    try:
        _emit_progress(50, "复制CubeMX工程文件")
        shutil.copytree(
            source,
            temporary,
            ignore=_clone_ignore(old_name),
            copy_function=shutil.copy2,
        )
        _emit_progress(72, "重命名IOC、Keil工程和Target")
        project_file = _rename_project_files(temporary, old_name, new_name)
        if use_junction:
            _create_sdk_link(temporary, sdk_root)
        else:
            _rewrite_keil_sdk_path(project_file, sdk_reference)
        _emit_progress(90, "发布克隆工程")
        temporary.replace(destination)
        project_file = destination / project_file.relative_to(temporary)
    except Exception:
        if temporary.exists() and (temporary.parent == destination.parent):
            shutil.rmtree(temporary)
        raise

    print(f"cloned: {source}")
    print(f"     to: {destination}")
    print(f"    ioc: {destination / (new_name + '.ioc')}")
    print(f"   keil: {project_file}")
    if use_junction:
        print(f"    sdk: {destination / 'ark_sdk'} -> {sdk_root}")
    else:
        print(f"    sdk: {sdk_reference} (relative from project root)")
    print("next: open the cloned .ioc, configure required peripherals, regenerate, then sync the selected App")
    _emit_progress(100, "CubeMX工程克隆完成")
    return 0


def _ioc_values(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#") and ("=" in line):
            key, value = line.split("=", 1)
            values[key] = value
    return values


def audit_usb_cdc(args: argparse.Namespace) -> int:
    project = args.project.resolve()
    ioc = _cube_ioc(project)
    values = _ioc_values(ioc)
    main = project / "Core" / "Src" / "main.c"
    hal_conf = project / "Core" / "Inc" / "stm32f1xx_hal_conf.h"
    irq_source = project / "Core" / "Src" / "stm32f1xx_it.c"
    keil_project = _keil_project(project)
    main_text = main.read_text(encoding="utf-8") if main.is_file() else ""
    hal_text = hal_conf.read_text(encoding="utf-8") if hal_conf.is_file() else ""
    irq_text = irq_source.read_text(encoding="utf-8") if irq_source.is_file() else ""
    keil_text = keil_project.read_text(encoding="utf-8")
    usb_component = SDK_ROOT / "component" / "usb" / "usb.c"
    usb_component_text = (
        usb_component.read_text(encoding="utf-8")
        if usb_component.is_file() else ""
    )
    usb_init_reachable = (
        "MX_USB_DEVICE_Init();" in main_text or
        (
            "ARK_USB_DEVICE_CUBEMX_INIT" in keil_text and
            "MX_USB_DEVICE_Init();" in usb_component_text
        )
    )

    middleware_keys = "\n".join(f"{key}={value}" for key, value in values.items())
    usb_irq = next(
        (value for key, value in values.items() if "USB_LP_CAN1_RX0_IRQn" in key),
        "",
    )
    generated_candidates = [
        project / "USB_DEVICE" / "App" / "usb_device.h",
        project / "USB_DEVICE" / "App" / "usbd_cdc_if.c",
        project / "USB_DEVICE" / "Target" / "usbd_conf.c",
        project / "Middlewares" / "ST" / "STM32_USB_Device_Library" / "Class" / "CDC" / "Inc" / "usbd_cdc.h",
    ]
    tasks = values.get("FREERTOS.Tasks01", "")
    heap_size = values.get("FREERTOS.configTOTAL_HEAP_SIZE", "0")
    try:
        heap_size_value = int(heap_size, 0)
    except ValueError:
        heap_size_value = 0
    checks = [
        ("STM32F103 target", values.get("Mcu.Family") == "STM32F1" and "STM32F103" in values.get("Mcu.CPN", "")),
        ("PA11 is USB_DM", values.get("PA11.Signal") == "USB_DM"),
        ("PA12 is USB_DP", values.get("PA12.Signal") == "USB_DP"),
        ("CAN1 no longer owns PA11/PA12", values.get("PA11.Signal") != "CAN_RX" and values.get("PA12.Signal") != "CAN_TX"),
        ("USB clock is 48 MHz", values.get("RCC.USBFreq_Value") == "48000000"),
        ("USB Device CDC middleware selected", "USB_DEVICE" in middleware_keys and "CDC" in middleware_keys),
        ("USB IRQ enabled at priority 5", usb_irq.startswith(r"true\:5\:0")),
        ("HAL PCD module enabled", re.search(
            r"(?m)^\s*#define\s+HAL_PCD_MODULE_ENABLED\b", hal_text
        ) is not None),
        ("MX_USB_DEVICE_Init is reachable", usb_init_reachable),
        ("USB IRQ handler calls HAL PCD", "USB_LP_CAN1_RX0_IRQHandler" in irq_text and "HAL_PCD_IRQHandler" in irq_text),
        ("USB CDC generated files exist", all(path.is_file() for path in generated_candidates)),
        ("FreeRTOS appStartTask is weak and dynamic", "appStartTask" in tasks and "As weak" in tasks and "Dynamic" in tasks),
        ("FreeRTOS heap is at least 8192 bytes", heap_size_value >= 8192),
        ("IWDG remains enabled", any(value == "IWDG" for key, value in values.items() if key.startswith("Mcu.IP"))),
        ("PC13 LED starts high/off", values.get("PC13-TAMPER-RTC.Signal") == "GPIO_Output" and values.get("PC13-TAMPER-RTC.PinState") == "GPIO_PIN_SET"),
    ]

    print(f"USB CDC audit: {ioc}")
    passed = 0
    for label, result in checks:
        print(f"[{'PASS' if result else 'FAIL'}] {label}")
        passed += int(result)
    enabled_ips = {
        value for key, value in values.items() if key.startswith("Mcu.IP")
    }
    unused = sorted(enabled_ips & {"I2C1", "I2C2", "SPI1", "TIM3", "USART1"})
    if unused:
        print("[WARN] unused usb_debug peripherals are still enabled: " + ", ".join(unused))
    print(f"result: {passed}/{len(checks)} checks passed")
    return 0 if passed == len(checks) else 1


def audit_paths(args: argparse.Namespace) -> int:
    root = args.root.resolve()
    app_root = root / "ark_sdk" / "app"
    problems: list[str] = []
    checked = 0

    for config in sorted(app_root.glob("*/*.dts")):
        try:
            app_config = load_app_config(config)
            checked += 2
            if root not in app_config.ioc_path.parents:
                problems.append(f"{config}: inferred IOC escapes workspace: {app_config.ioc_path}")
            if root not in app_config.keil_project.parents:
                problems.append(
                    f"{config}: inferred Keil project escapes workspace: {app_config.keil_project}")
        except (OSError, ValueError) as exc:
            problems.append(f"{config}: {exc}")

    for project in sorted(root.glob("ark_stm32_projects/*/MDK-ARM/*.uvprojx")):
        checked += 1
        text = project.read_text(encoding="utf-8")
        matches = sorted(set(re.findall(
            r"[A-Za-z]:[/\\](?![/\\])[^<;\r\n]+", text
        )))
        for match in matches:
            problems.append(f"{project}: absolute path: {match}")

        sdk_entry = project.parent.parent / "ark_sdk"
        is_junction = bool(getattr(os.path, "isjunction", lambda _: False)(sdk_entry))
        if sdk_entry.is_symlink() or is_junction:
            problems.append(
                f"{sdk_entry}: SDK link may contain a machine-specific target; "
                "prefer direct relative Keil paths"
            )

    print(f"path audit: {root}")
    for problem in problems:
        print(f"[FAIL] {problem}")
    if not problems:
        print(f"[PASS] {checked} portable path entries checked")
    return 1 if problems else 0


def launch_gui(args: argparse.Namespace) -> int:
    try:
        import customtkinter as ctk
        from tkinter import filedialog, messagebox
    except ImportError as exc:
        raise ValueError(
            "GUI requires CustomTkinter; run: python -m pip install customtkinter"
        ) from exc

    ctk.set_appearance_mode("System")
    ctk.set_default_color_theme("blue")
    window = ctk.CTk()
    window.title("ARK CREW SDK - Cube 工程克隆工具")
    window.geometry("900x650")
    window.minsize(760, 560)
    window.grid_columnconfigure(1, weight=1)
    window.grid_rowconfigure(6, weight=1)

    source_var = ctk.StringVar(value=str((SDK_ROOT.parent / "ark_stm32_projects" / "c8t6_demo").resolve()))
    parent_var = ctk.StringVar(value=str((SDK_ROOT.parent / "ark_stm32_projects").resolve()))
    name_var = ctk.StringVar(value="c8t6_new_project")
    sdk_var = ctk.StringVar(value=str(SDK_ROOT))
    junction_var = ctk.BooleanVar(value=False)

    def choose_dir(variable: object) -> None:
        selected = filedialog.askdirectory(initialdir=str(Path(variable.get()).parent))
        if selected:
            variable.set(selected)

    def add_row(row: int, label: str, variable: object, callback: object) -> None:
        ctk.CTkLabel(window, text=label, width=110, anchor="e").grid(
            row=row, column=0, padx=(14, 8), pady=7, sticky="e"
        )
        ctk.CTkEntry(window, textvariable=variable).grid(
            row=row, column=1, padx=4, pady=7, sticky="ew"
        )
        ctk.CTkButton(window, text="选择", width=72, command=callback).grid(
            row=row, column=2, padx=(8, 14), pady=7
        )

    add_row(0, "源 Cube 工程", source_var, lambda: choose_dir(source_var))
    add_row(1, "目标父目录", parent_var, lambda: choose_dir(parent_var))
    ctk.CTkLabel(window, text="新工程名", width=110, anchor="e").grid(
        row=2, column=0, padx=(14, 8), pady=7, sticky="e"
    )
    ctk.CTkEntry(window, textvariable=name_var).grid(
        row=2, column=1, columnspan=2, padx=(4, 14), pady=7, sticky="ew"
    )
    add_row(3, "SDK 目录", sdk_var, lambda: choose_dir(sdk_var))

    options = ctk.CTkFrame(window, fg_color="transparent")
    options.grid(row=5, column=0, columnspan=3, padx=14, pady=(3, 8), sticky="ew")
    ctk.CTkCheckBox(
        options,
        text="创建工程内 ark_sdk 目录联接（默认关闭；相对路径更便于迁移）",
        variable=junction_var,
    ).pack(side="left")

    log = ctk.CTkTextbox(window, wrap="word", font=("Consolas", 12))
    log.grid(row=6, column=0, columnspan=3, padx=14, pady=8, sticky="nsew")

    def write_log(content: str) -> None:
        log.insert("end", content.rstrip() + "\n")
        log.see("end")

    def make_args(dry_run: bool) -> argparse.Namespace:
        name = name_var.get().strip()
        return argparse.Namespace(
            source=Path(source_var.get().strip()),
            destination=Path(parent_var.get().strip()) / name,
            name=name,
            sdk_root=Path(sdk_var.get().strip()),
            dry_run=dry_run,
            sdk_junction=junction_var.get(),
        )

    def execute(dry_run: bool) -> None:
        output = io.StringIO()
        try:
            with redirect_stdout(output):
                clone_project(make_args(dry_run))
            write_log(output.getvalue())
            if not dry_run:
                messagebox.showinfo("完成", "工程克隆和重命名已完成。")
        except (OSError, ValueError, json.JSONDecodeError) as exc:
            write_log(f"[ERROR] {exc}")
            messagebox.showerror("失败", str(exc))

    buttons = ctk.CTkFrame(window, fg_color="transparent")
    buttons.grid(row=7, column=0, columnspan=3, padx=14, pady=(5, 14), sticky="ew")
    ctk.CTkButton(buttons, text="检查配置", command=lambda: execute(True)).pack(side="left", padx=(0, 8))
    ctk.CTkButton(buttons, text="一键克隆", command=lambda: execute(False)).pack(side="left", padx=8)
    ctk.CTkButton(buttons, text="清空日志", command=lambda: log.delete("1.0", "end")).pack(side="right")

    write_log("默认使用相对 SDK 路径。选择源工程、目标父目录并输入新工程名即可克隆。")
    window.mainloop()
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command")

    clone = subparsers.add_parser("clone", help="Clone and rename a CubeMX/Keil project")
    clone.add_argument("source", type=Path, help="Source CubeMX project directory")
    clone.add_argument("destination", type=Path, help="New project directory; must not exist")
    clone.add_argument("--name", help="New CubeMX and Keil project/target name")
    clone.add_argument("--sdk-root", type=Path, default=SDK_ROOT, help="SDK directory referenced by the cloned project")
    clone.add_argument("--sdk-junction", action="store_true", help="Create a project-root ark_sdk junction instead of direct relative paths")
    clone.add_argument("--dry-run", action="store_true", help="Validate and print actions without copying")
    clone.add_argument("--check", action="store_true", help="Check clone inputs without copying")
    clone.set_defaults(func=clone_project)

    audit = subparsers.add_parser("audit-usb-cdc", help="Audit generated STM32F103 USB CDC files and .ioc")
    audit.add_argument("project", type=Path, help="CubeMX project directory")
    audit.set_defaults(func=audit_usb_cdc)

    paths = subparsers.add_parser("audit-paths", help="Audit app and Keil configuration path portability")
    paths.add_argument("root", nargs="?", type=Path, default=SDK_ROOT.parent, help="Workspace root containing ark_sdk")
    paths.set_defaults(func=audit_paths)

    gui = subparsers.add_parser("gui", help="Open the graphical clone tool")
    gui.set_defaults(func=launch_gui)

    args = parser.parse_args()
    if args.command is None:
        args.func = launch_gui
    try:
        return args.func(args)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        parser.error(str(exc))


if __name__ == "__main__":
    raise SystemExit(main())
