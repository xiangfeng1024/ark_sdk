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

"""Create and rebuild a self-contained CubeMX and ARK CREW SDK source package."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import tempfile
import time
import xml.etree.ElementTree as ET

from studio.tooling.keil_tools import find_uv4, run_uv4
from studio.tooling.project_config import (
    SDK_ROOT,
    AppConfig,
    BuildSelection,
    component_dts_catalog,
    detect_synced_app,
    load_app_config,
    resolve_selection,
    sync_keil_project,
)
from studio.tooling.ark_dts import generate_for_app


PACKAGE_SUFFIX = "_package"
BUILD_FILE_SUFFIXES = {
    ".axf",
    ".build_log.htm",
    ".crf",
    ".d",
    ".dep",
    ".hex",
    ".htm",
    ".lnp",
    ".lst",
    ".map",
    ".o",
    ".sct",
}


@dataclass(frozen=True)
class PackageResult:
    path: Path
    project: Path
    source_files: int
    total_bytes: int


def _emit_progress(percent: int, message: str) -> None:
    print(f"[ARK_PROGRESS] {percent} {message}", flush=True)


def _is_within(path: Path, root: Path) -> bool:
    path = path.resolve()
    root = root.resolve()
    return path == root or root in path.parents


def _cube_project_root(project: Path) -> Path:
    for candidate in (project.parent, *project.parents):
        if any(candidate.glob("*.ioc")) and (candidate / "Core").is_dir():
            return candidate.resolve()
    raise ValueError(f"CubeMX project root was not found for: {project}")


def _target_node(project: Path, target: str) -> ET.Element:
    root = ET.parse(project).getroot()
    for candidate in root.findall(".//Target"):
        if candidate.findtext("TargetName") == target:
            return candidate
    raise ValueError(f"target not found in project: {target}")


def _build_output_paths(project: Path, target: str, cube_root: Path) -> set[Path]:
    target_node = _target_node(project, target)
    paths = {project.parent / ".keil"}
    for tag in ("OutputDirectory", "ListingPath"):
        value = (target_node.findtext(f"./TargetOption/TargetCommonOption/{tag}") or "").strip()
        if not value:
            continue
        path = (project.parent / value.replace("\\", os.sep)).resolve()
        if not _is_within(path, cube_root) or path == cube_root:
            raise ValueError(f"unsafe Keil {tag}: {path}")
        paths.add(path)
    return {path.resolve() for path in paths}


def _copy_cube_project(
    source_root: Path,
    destination_root: Path,
    excluded_paths: set[Path],
) -> None:
    excluded_relative = {
        path.relative_to(source_root)
        for path in excluded_paths
        if _is_within(path, source_root) and path != source_root
    }

    def ignore(directory: str, names: list[str]) -> set[str]:
        directory_path = Path(directory).resolve()
        relative_directory = directory_path.relative_to(source_root)
        ignored: set[str] = set()
        for name in names:
            relative = relative_directory / name
            source = directory_path / name
            if relative in excluded_relative:
                ignored.add(name)
                continue
            if name in {".git", "__pycache__"}:
                ignored.add(name)
                continue
            if source.is_file() and (
                ".uvguix." in name.lower() or source.suffix.lower() in BUILD_FILE_SUFFIXES
            ):
                ignored.add(name)
        return ignored

    shutil.copytree(
        source_root,
        destination_root,
        dirs_exist_ok=True,
        symlinks=False,
        ignore=ignore,
    )


def _copy_path(source: Path, destination: Path) -> None:
    if source.is_dir():
        shutil.copytree(
            source,
            destination,
            dirs_exist_ok=True,
            symlinks=False,
            ignore=shutil.ignore_patterns("__pycache__", "*.pyc", "*.pyo"),
        )
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def _copy_selected_sdk(
    destination_sdk: Path,
    config: AppConfig,
    selection: BuildSelection,
) -> None:
    directories = {
        SDK_ROOT / "hal" / "include",
        SDK_ROOT / "component" / "common",
        SDK_ROOT / "app" / config.app,
    }
    platform_include = SDK_ROOT / "hal" / config.platform / "include"
    if platform_include.is_dir():
        directories.add(platform_include)
    component_catalog = component_dts_catalog()
    component_groups = {
        component_catalog[name].group for name in config.components
    }

    for source in sorted(directories):
        _copy_path(source, destination_sdk / source.relative_to(SDK_ROOT))

    for group in sorted(component_groups):
        source_dir = SDK_ROOT / "component" / group
        destination_dir = destination_sdk / "component" / group
        for header in source_dir.glob("*.h"):
            _copy_path(header, destination_dir / header.name)

    for sources in selection.groups.values():
        for value in sources:
            if value.startswith("@project/"):
                continue
            source = SDK_ROOT / value
            _copy_path(source, destination_sdk / value)

    component_document = json.loads(
        (SDK_ROOT / "component" / "common" / "component_catalog.json").read_text(
            encoding="utf-8"))
    component_document["components"] = [
        item for item in component_document["components"]
        if item.get("name") in config.components
    ]
    component_catalog_path = (
        destination_sdk / "component" / "common" / "component_catalog.json")
    component_catalog_path.write_text(
        json.dumps(component_document, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )

    hal_document = json.loads(
        (SDK_ROOT / "hal" / "common" / "hal_catalog.json").read_text(
            encoding="utf-8"))
    hal_document["platforms"] = [
        {
            **platform,
            "drivers": [
                driver for driver in platform["drivers"]
                if driver.get("name") in config.hal
            ],
        }
        for platform in hal_document["platforms"]
        if platform.get("name") == config.platform
    ]
    hal_catalog_path = destination_sdk / "hal" / "common" / "hal_catalog.json"
    hal_catalog_path.parent.mkdir(parents=True, exist_ok=True)
    hal_catalog_path.write_text(
        json.dumps(hal_document, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def _validate_sdk_scope(destination_sdk: Path, config: AppConfig) -> None:
    forbidden = ("camera", "doc", "log", "studio", "skills")
    leaked = [name for name in forbidden if (destination_sdk / name).exists()]
    if leaked:
        raise ValueError("customer package contains private SDK directories: " + ", ".join(leaked))

    app_root = destination_sdk / "app"
    app_names = sorted(path.name for path in app_root.iterdir() if path.is_dir())
    if app_names != [config.app]:
        raise ValueError("customer package contains unexpected SDK apps: " + ", ".join(app_names))

    component_root = destination_sdk / "component"
    catalog = component_dts_catalog()
    expected_components = {
        "common", *(catalog[name].group for name in config.components)
    }
    component_names = {path.name for path in component_root.iterdir() if path.is_dir()}
    if component_names != expected_components:
        raise ValueError("customer package SDK component scope does not match the selected app")


def _managed_prefix(sdk_link: str) -> str:
    return str(PurePosixPath("..") / sdk_link).rstrip("/").replace("\\", "/") + "/"


def _replace_sdk_link(project: Path, old_link: str, new_link: str) -> None:
    tree = ET.parse(project)
    root = tree.getroot()
    old_prefix = _managed_prefix(old_link)
    new_prefix = _managed_prefix(new_link)

    for node in root.findall(".//FilePath"):
        value = (node.text or "").replace("\\", "/")
        if value.startswith(old_prefix):
            node.text = new_prefix + value[len(old_prefix):]

    for node in root.findall(".//IncludePath"):
        entries = []
        for entry in (node.text or "").split(";"):
            normalized = entry.replace("\\", "/")
            if normalized.startswith(old_prefix):
                normalized = new_prefix + normalized[len(old_prefix):]
            entries.append(normalized)
        node.text = ";".join(entries)

    ET.indent(tree, space="  ")
    tree.write(project, encoding="utf-8", xml_declaration=True)


def _resolve_keil_path(project: Path, value: str) -> Path:
    normalized = value.replace("\\", os.sep).replace("/", os.sep)
    path = Path(normalized)
    if not path.is_absolute():
        path = project.parent / path
    return path.resolve()


def _validate_self_contained(project: Path, package_root: Path) -> None:
    root = ET.parse(project).getroot()
    missing: list[str] = []
    external: list[str] = []

    values = [node.text or "" for node in root.findall(".//FilePath")]
    for node in root.findall(".//IncludePath"):
        values.extend(entry for entry in (node.text or "").split(";") if entry)

    for value in values:
        if not value or "$" in value or "%" in value:
            continue
        path = _resolve_keil_path(project, value)
        if not _is_within(path, package_root):
            external.append(value)
        elif not path.exists():
            missing.append(value)
    if external:
        raise ValueError("package still contains external paths: " + ", ".join(sorted(set(external))))
    if missing:
        raise ValueError("package contains missing paths: " + ", ".join(sorted(set(missing))))

    links = [path for path in package_root.rglob("*") if path.is_symlink()]
    if links:
        raise ValueError("package contains symbolic links: " + ", ".join(str(path) for path in links))


def _clean_build_outputs(project: Path, target: str, package_root: Path) -> None:
    for path in _build_output_paths(project, target, package_root):
        if path.is_dir():
            shutil.rmtree(path)
        elif path.is_file():
            path.unlink()
    for item in project.parent.iterdir():
        if not item.is_file():
            continue
        if ".uvguix." in item.name.lower() or item.suffix.lower() in BUILD_FILE_SUFFIXES:
            item.unlink()


def _replace_directory(source: Path, destination: Path) -> None:
    last_error: OSError | None = None
    for _attempt in range(40):
        try:
            source.replace(destination)
            return
        except OSError as exc:
            last_error = exc
            time.sleep(0.25)
    if last_error is not None:
        raise last_error


def _remove_tree(source: Path) -> None:
    for _attempt in range(20):
        try:
            shutil.rmtree(source)
            return
        except OSError:
            time.sleep(0.25)


def _write_package_info(
    package_root: Path,
    config: AppConfig,
    project: Path,
    source_files: int,
) -> None:
    relative_project = project.relative_to(package_root).as_posix()
    content = (
        "# Standalone Keil Source Package\n\n"
        f"- App: `{config.app}`\n"
        f"- Target: `{config.target}`\n"
        f"- Keil project: `{relative_project}`\n"
        f"- Packaged: `{datetime.now().astimezone().isoformat(timespec='seconds')}`\n"
        f"- Source files: `{source_files}`\n"
        "- Validation: full Keil rebuild completed successfully before delivery.\n\n"
        "Run `kill.bat` from the package root to remove Keil target outputs and tool logs "
        "after rebuilding without deleting source code or package validation records.\n\n"
        "Open the `.uvprojx` file above and rebuild the target. All CubeMX, vendor, "
        "middleware, selected ARK CREW SDK HAL, component, and application sources are stored "
        "inside this directory. No source path depends on the original SDK checkout.\n"
    )
    (package_root / "PACKAGE_INFO.md").write_text(content, encoding="utf-8")


def _write_kill_script(
    package_root: Path,
    project: Path,
    target: str,
) -> None:
    project_relative = project.relative_to(package_root)
    project_directory = project_relative.parent
    project_relative_value = str(project_relative).replace("/", "\\")
    output_paths = sorted(_build_output_paths(project, target, package_root))
    lines = [
        "@echo off",
        "setlocal",
        "pushd \"%~dp0\" || exit /b 1",
        "if not exist \"PACKAGE_INFO.md\" (",
        "  echo Refusing to clean: PACKAGE_INFO.md was not found.",
        "  popd",
        "  exit /b 2",
        ")",
        f"if not exist \"{project_relative_value}\" (",
        "  echo Refusing to clean: packaged Keil project was not found.",
        "  popd",
        "  exit /b 2",
        ")",
    ]
    for path in output_paths:
        relative = str(path.relative_to(package_root)).replace("/", "\\")
        lines.extend(
            (
                f"if exist \"{relative}\" rmdir /s /q \"{relative}\"",
                "if errorlevel 1 goto :failed",
            )
        )
    project_directory_value = str(project_directory).replace("/", "\\")
    for suffix in sorted(BUILD_FILE_SUFFIXES):
        lines.append(
            f"del /f /q \"{project_directory_value}\\*{suffix}\" >nul 2>&1"
        )
    lines.extend(
        (
            "popd",
            "echo Build outputs removed.",
            "exit /b 0",
            "",
            ":failed",
            "popd",
            "echo Failed to remove one or more build output directories.",
            "exit /b 1",
            "",
        )
    )
    with (package_root / "kill.bat").open("w", encoding="ascii", newline="\r\n") as stream:
        stream.write("\n".join(lines))


def create_standalone_package(
    config_path: Path,
    output_parent: Path,
    uv4_value: str | None = None,
) -> PackageResult:
    config_path = config_path.resolve()
    output_parent = output_parent.resolve()
    if not output_parent.is_dir():
        raise ValueError(f"output parent must be an existing directory: {output_parent}")

    generate_for_app(config_path)
    config = load_app_config(config_path)
    selection = resolve_selection(config)
    source_project = config.keil_project.resolve()
    source_root = _cube_project_root(source_project)
    if _is_within(output_parent, source_root) or _is_within(output_parent, SDK_ROOT):
        raise ValueError("output parent must be outside the CubeMX project and SDK directories")

    package_name = source_root.name + PACKAGE_SUFFIX
    final_root = output_parent / package_name
    if final_root.exists():
        raise ValueError(f"package destination already exists: {final_root}")

    _emit_progress(5, "validate package inputs")
    uv4 = find_uv4(uv4_value)
    _emit_progress(10, "synchronize selected app into Keil")
    sync_keil_project(
        source_project,
        selection,
        config.sdk_link,
        config.device,
        config.reset_and_run,
    )
    if detect_synced_app(source_project) != config.app:
        raise ValueError("selected app was not synchronized into the Keil project")
    staging = Path(tempfile.mkdtemp(prefix=f".{package_name}.", dir=output_parent)).resolve()
    try:
        _emit_progress(15, "copy CubeMX project")
        excluded = _build_output_paths(source_project, config.target, source_root)
        _copy_cube_project(source_root, staging, excluded)

        _emit_progress(35, "copy selected SDK dependency closure")
        packaged_sdk = staging / "ark_sdk"
        _copy_selected_sdk(packaged_sdk, config, selection)
        _validate_sdk_scope(packaged_sdk, config)

        relative_project = source_project.relative_to(source_root)
        packaged_project = staging / relative_project
        _emit_progress(45, "rewrite Keil source paths")
        _replace_sdk_link(packaged_project, config.sdk_link, "ark_sdk")
        sync_keil_project(
            packaged_project,
            selection,
            "ark_sdk",
            config.device,
            config.reset_and_run,
        )

        _emit_progress(55, "verify self-contained paths")
        _validate_self_contained(packaged_project, staging)

        _emit_progress(60, "run full Keil rebuild")
        build_log = staging / "PACKAGE_BUILD.log"
        result = run_uv4(uv4, "-r", packaged_project, config.target, build_log)
        if result != 0:
            raise ValueError("packaged Keil project failed its full rebuild")

        _emit_progress(95, "remove temporary build outputs")
        _clean_build_outputs(packaged_project, config.target, staging)
        _validate_self_contained(packaged_project, staging)
        source_files = sum(1 for path in staging.rglob("*") if path.is_file())
        _write_package_info(staging, config, packaged_project, source_files)
        _write_kill_script(staging, packaged_project, config.target)
        source_files += 2
        total_bytes = sum(path.stat().st_size for path in staging.rglob("*") if path.is_file())
        _replace_directory(staging, final_root)
        _emit_progress(100, "standalone package complete")
        print(f"Package: {final_root}")
        print(f"Keil project: {final_root / relative_project}")
        print(f"Files: {source_files}")
        print(f"Bytes: {total_bytes}")
        return PackageResult(final_root, final_root / relative_project, source_files, total_bytes)
    except Exception:
        if staging.exists():
            _remove_tree(staging)
        raise


def check_package_environment(
    config_path: Path,
    output_parent: Path,
    uv4_value: str | None = None,
) -> None:
    _emit_progress(20, "validate package configuration")
    config = load_app_config(config_path.resolve())
    resolve_selection(config)
    if not output_parent.resolve().is_dir():
        raise ValueError(f"output parent must be an existing directory: {output_parent}")
    source_root = _cube_project_root(config.keil_project)
    if _is_within(output_parent.resolve(), source_root) or _is_within(
        output_parent.resolve(), SDK_ROOT
    ):
        raise ValueError("output parent must be outside the CubeMX project and SDK directories")
    final_root = output_parent.resolve() / (source_root.name + PACKAGE_SUFFIX)
    if final_root.exists():
        raise ValueError(f"package destination already exists: {final_root}")
    _emit_progress(70, "validate Keil toolchain")
    uv4 = find_uv4(uv4_value)
    print(f"package output: {final_root}")
    print(f"Keil: {uv4}")
    _emit_progress(100, "package environment check passed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config", type=Path, required=True,
        help="Path to app/<name>/<name>.dts")
    parser.add_argument("--output", type=Path, required=True, help="Existing parent directory for the package folder")
    parser.add_argument("--uv4", help="Explicit UV4.exe path")
    parser.add_argument("--check", action="store_true", help="Check environment without creating a package")
    args = parser.parse_args()
    try:
        if args.check:
            check_package_environment(args.config, args.output, args.uv4)
        else:
            create_standalone_package(args.config, args.output, args.uv4)
        return 0
    except (OSError, ValueError, ET.ParseError) as exc:
        print(f"error: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
