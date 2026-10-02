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

"""Inspect and convert supported ARK CREW SDK text files between UTF-8 and GB2312."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime
import json
import os
from pathlib import Path
import shutil
import tempfile


TEXT_SUFFIXES = {".c", ".h", ".py", ".ts", ".tsx", ".css", ".json", ".dts", ".md"}
SKIP_DIRECTORY_NAMES = {
    ".git", "node_modules", "__pycache__", "dist", "build", "Objects", "Listings",
    ".venv", "venv", ".idea", ".vscode",
}


@dataclass(frozen=True)
class EncodingItem:
    path: Path
    encoding: str
    reason: str = ""


def emit_progress(percent: int, message: str) -> None:
    print(f"[ARK_PROGRESS] {percent} {message}", flush=True)


def sdk_files(root: Path) -> list[Path]:
    result: list[Path] = []

    for path in root.rglob("*"):
        if any(part in SKIP_DIRECTORY_NAMES for part in path.relative_to(root).parts[:-1]):
            continue
        if path.is_file() and path.suffix.lower() in TEXT_SUFFIXES:
            result.append(path)
    return sorted(result)


def detect_encoding(path: Path) -> EncodingItem:
    data = path.read_bytes()

    if b"\0" in data:
        return EncodingItem(path, "binary", "contains NUL byte")
    try:
        data.decode("utf-8-sig")
        return EncodingItem(path, "utf-8")
    except UnicodeDecodeError:
        pass
    try:
        data.decode("gb2312")
        return EncodingItem(path, "gb2312")
    except UnicodeDecodeError:
        return EncodingItem(path, "unknown", "not valid UTF-8 or GB2312")


def scan(root: Path) -> list[EncodingItem]:
    return [detect_encoding(path) for path in sdk_files(root)]


def backup_root() -> Path:
    local = Path(os.environ.get("LOCALAPPDATA", Path.home() / ".local"))
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    return local / "ARKCrewStudio" / "encoding-backups" / stamp


def convert(root: Path, target: str) -> tuple[int, dict[str, object]]:
    items = scan(root)
    convertible = [item for item in items if item.encoding in {"utf-8", "gb2312"} and item.encoding != target]
    unknown = [item for item in items if item.encoding == "unknown"]
    report: dict[str, object] = {
        "root": str(root),
        "target": target,
        "total": len(items),
        "convertible": [str(item.path.relative_to(root)) for item in convertible],
        "unknown": [str(item.path.relative_to(root)) for item in unknown],
        "backup": "",
    }
    if not convertible:
        return 0, report
    destination = backup_root()
    for index, item in enumerate(convertible, start=1):
        relative = item.path.relative_to(root)
        backup = destination / relative
        backup.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(item.path, backup)
        text = item.path.read_bytes().decode("utf-8-sig" if item.encoding == "utf-8" else "gb2312")
        encoded = text.encode("utf-8" if target == "utf-8" else "gb2312")
        with tempfile.NamedTemporaryFile("wb", delete=False, dir=item.path.parent, suffix=".encoding.tmp") as temp:
            temporary = Path(temp.name)
            temp.write(encoded)
        os.replace(temporary, item.path)
        emit_progress(10 + (index * 85 // len(convertible)), f"已转换 {index}/{len(convertible)}: {relative.as_posix()}")
    report["backup"] = str(destination)
    destination.mkdir(parents=True, exist_ok=True)
    (destination / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return 0, report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", "--sdk-root", dest="root", type=Path, required=True)
    parser.add_argument(
        "--target", type=str.lower, choices=("utf-8", "gb2312"),
        default="utf-8",
    )
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    if not root.is_dir():
        print(f"error: invalid source directory: {root}")
        return 1
    emit_progress(5, "开始扫描SDK文本文件")
    if args.check:
        items = scan(root)
        summary = {encoding: sum(item.encoding == encoding for item in items) for encoding in ("utf-8", "gb2312", "unknown")}
        print(json.dumps(summary, ensure_ascii=False))
        emit_progress(100, "编码检查完成")
        return 0
    code, report = convert(root, args.target)
    print(json.dumps(report, ensure_ascii=False, indent=2))
    if code == 0:
        emit_progress(100, "无需转换" if not report["convertible"] else "编码转换完成")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
