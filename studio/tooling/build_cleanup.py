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

"""Safely discover and remove Keil build outputs with observable progress."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import stat
from typing import Callable, Iterable
import xml.etree.ElementTree as ET

from studio.tooling.project_config import load_app_config


ProgressCallback = Callable[[int, str, str], None]
CancelCallback = Callable[[], bool]


class CleanupCancelled(RuntimeError):
    """Raised when a cancellable cleanup is stopped by the task engine."""


@dataclass(frozen=True)
class CleanupPlan:
    project_dir: Path
    roots: tuple[Path, ...]
    files: tuple[Path, ...]
    directories: tuple[Path, ...]
    total_bytes: int


def _progress(callback: ProgressCallback | None, percent: int, stage: str, message: str) -> None:
    if callback is not None:
        callback(percent, stage, message)


def _cancelled(callback: CancelCallback | None) -> None:
    if callback is not None and callback():
        raise CleanupCancelled("cleanup cancelled")


def _safe_child(project_dir: Path, value: str, label: str) -> Path:
    target = (project_dir / value).resolve()
    if target == project_dir or project_dir not in target.parents:
        raise ValueError(f"refusing to clean unsafe {label}: {target}")
    return target


def _is_reparse_point(path: Path) -> bool:
    try:
        attributes = getattr(path.lstat(), "st_file_attributes", 0)
    except OSError:
        return False
    return bool(attributes & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400))


def _walk_root(root: Path, cancelled: CancelCallback | None) -> Iterable[tuple[Path, bool]]:
    if not root.exists():
        return
    if _is_reparse_point(root):
        raise ValueError(f"refusing to clean reparse point: {root}")
    for current, directory_names, file_names in os.walk(root, topdown=True, followlinks=False):
        _cancelled(cancelled)
        current_path = Path(current)
        for name in directory_names:
            child = current_path / name
            if _is_reparse_point(child):
                raise ValueError(f"refusing to clean reparse point: {child}")
        for name in file_names:
            yield current_path / name, False
        yield current_path, True


def create_cleanup_plan(
    config_path: Path,
    progress: ProgressCallback | None = None,
    cancelled: CancelCallback | None = None,
) -> CleanupPlan:
    config = load_app_config(config_path.resolve())
    project = config.keil_project.resolve()
    project_dir = project.parent
    root = ET.parse(project).getroot()
    target = next((node for node in root.findall(".//Target") if node.findtext("TargetName") == config.target), None)
    if target is None:
        raise ValueError(f"target not found in project: {config.target}")
    _progress(progress, 5, "inspect", "已读取Keil target配置")

    values = (
        ((target.findtext("./TargetOption/TargetCommonOption/OutputDirectory") or "").strip(), "output directory"),
        ((target.findtext("./TargetOption/TargetCommonOption/ListingPath") or "").strip(), "listing directory"),
        (".keil", "tool log directory"),
    )
    roots = tuple(sorted({_safe_child(project_dir, value, label) for value, label in values if value}))
    _progress(progress, 10, "inspect", f"已确认{len(roots)}个安全清理目录")

    files: set[Path] = set()
    directories: set[Path] = set()
    total_bytes = 0
    for index, cleanup_root in enumerate(roots, 1):
        for item, is_directory in _walk_root(cleanup_root, cancelled):
            if is_directory:
                directories.add(item)
            else:
                files.add(item)
                try:
                    total_bytes += item.stat().st_size
                except OSError:
                    pass
        _progress(progress, 10 + round(index * 10 / max(len(roots), 1)), "scan", f"已扫描 {cleanup_root.name}")

    patterns = ("*.lst", "*.map", "*.axf", "*.hex", "*.lnp", "*.sct", "*.dep", "*.build_log.htm")
    for pattern in patterns:
        _cancelled(cancelled)
        for item in project_dir.glob(pattern):
            if item.is_file():
                resolved = item.resolve()
                if project_dir not in resolved.parents:
                    raise ValueError(f"refusing to clean project-external file: {resolved}")
                files.add(resolved)
                try:
                    total_bytes += resolved.stat().st_size
                except OSError:
                    pass
    _progress(progress, 20, "scan", f"扫描完成，共{len(files)}个文件")
    return CleanupPlan(
        project_dir=project_dir,
        roots=roots,
        files=tuple(sorted(files)),
        directories=tuple(sorted(directories, key=lambda path: len(path.parts), reverse=True)),
        total_bytes=total_bytes,
    )


def execute_cleanup_plan(
    plan: CleanupPlan,
    progress: ProgressCallback | None = None,
    cancelled: CancelCallback | None = None,
) -> int:
    total = len(plan.files) + len(plan.directories)
    if total == 0:
        _progress(progress, 92, "clean", "构建产物已经为空")
        return 0
    completed = 0
    for path in plan.files:
        _cancelled(cancelled)
        try:
            path.unlink(missing_ok=True)
        except OSError as exc:
            raise OSError(f"cannot remove build output {path}: {exc}") from exc
        completed += 1
        _progress(progress, 20 + round(completed * 72 / total), "clean", f"正在清理 {completed}/{total}")
    for path in plan.directories:
        _cancelled(cancelled)
        try:
            path.rmdir()
        except FileNotFoundError:
            pass
        except OSError as exc:
            raise OSError(f"cannot remove build directory {path}: {exc}") from exc
        completed += 1
        _progress(progress, 20 + round(completed * 72 / total), "clean", f"正在清理 {completed}/{total}")
    _progress(progress, 92, "clean", f"已删除{len(plan.files)}个文件")
    return len(plan.files)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    plan = create_cleanup_plan(args.config)
    print(f"files={len(plan.files)} bytes={plan.total_bytes}")
    if args.dry_run:
        return 0
    execute_cleanup_plan(plan)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
