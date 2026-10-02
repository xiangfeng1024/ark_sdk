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

"""DTS application discovery and Keil project synchronization primitives."""

from __future__ import annotations

from dataclasses import dataclass
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import tempfile
from typing import Any
import xml.etree.ElementTree as ET

from studio.tooling.dts_parser import DtsDocument, DtsNode, parse_dts


SDK_ROOT = Path(os.environ.get("ARK_SDK_ROOT", Path(__file__).resolve().parents[2])).resolve()
SOURCE_SUFFIXES = {".c": "1", ".s": "2", ".asm": "2", ".cpp": "8", ".cxx": "8"}
IDENTIFIER_PATTERN = re.compile(r"^[a-z0-9][a-z0-9_-]*$")
APP_PROJECT_ALIASES = {"c8t6_ark_net": ("c8t6_xiaoyan_net",)}


def normalize_sdk_path(value: str) -> str:
    """Return a safe project-root-relative SDK path using portable separators."""
    if not isinstance(value, str) or not value.strip():
        raise ValueError("inferred SDK link must be a non-empty relative path")
    normalized = value.strip().replace("\\", "/")
    path = PurePosixPath(normalized)
    if path.is_absolute() or re.match(r"^[A-Za-z]:", normalized):
        raise ValueError("inferred SDK link must be relative to the Cube project root")
    if normalized in {".", ".."} or not path.name:
        raise ValueError("inferred SDK link must resolve to an SDK directory")
    return path.as_posix()


@dataclass(frozen=True)
class AppConfig:
    path: Path
    app: str
    cube_project_root: Path
    ioc_path: Path
    platform: str
    project_prefix: str
    hal: tuple[str, ...]
    components: tuple[str, ...]
    keil_project: Path
    keil_project_value: str
    target: str
    sdk_link: str
    device: str
    reset_and_run: bool
    flash_reserve_bytes: int = 0


@dataclass(frozen=True)
class CubeProjectBinding:
    root: Path
    ioc: Path
    keil_project: Path
    target: str
    sdk_link: str


@dataclass(frozen=True)
class KeilDeviceProfile:
    device: str
    vendor: str
    pack_id: str
    pack_url: str
    cpu: str
    flash_driver: str
    register_file: str
    svd_file: str
    flash_algorithm: str
    flash_start: str
    flash_length: str


KEIL_DEVICE_PROFILES = {
    "STM32F103C8": KeilDeviceProfile(
        device="STM32F103C8",
        vendor="STMicroelectronics",
        pack_id="Keil.STM32F1xx_DFP.2.4.1",
        pack_url="https://www.keil.com/pack/",
        cpu='IRAM(0x20000000,0x00005000) IROM(0x08000000,0x00010000) CPUTYPE("Cortex-M3") CLOCK(12000000) ELITTLE',
        flash_driver="UL2CM3(-S0 -C0 -P0 -FD20000000 -FC1000 -FN1 -FF0STM32F10x_128 -FS08000000 -FL020000 -FP0($$Device:STM32F103C8$Flash\\STM32F10x_128.FLM))",
        register_file="$$Device:STM32F103C8$Device\\Include\\stm32f10x.h",
        svd_file="$$Device:STM32F103C8$SVD\\STM32F103xx.svd",
        flash_algorithm="STM32F10x_128",
        flash_start="08000000",
        flash_length="020000",
    ),
    "GD32F103C8": KeilDeviceProfile(
        device="GD32F103C8",
        vendor="GigaDevice",
        pack_id="GigaDevice.GD32F10x_DFP.2.0.3",
        pack_url="https://gd32mcu.com/data/documents/pack/",
        cpu='IRAM(0x20000000,0x00005000) IROM(0x08000000,0x00010000) CPUTYPE("Cortex-M3") CLOCK(12000000) ELITTLE',
        flash_driver="UL2CM3(-S0 -C0 -P0 -FD20000000 -FC1000 -FN1 -FF0GD32F10x_MD -FS08000000 -FL010000 -FP0($$Device:GD32F103C8$Flash\\GD32F10x_MD.FLM))",
        register_file="$$Device:GD32F103C8$Device\\Include\\gd32f10x.h",
        svd_file="$$Device:GD32F103C8$SVD\\GD32F10x\\GD32F10x_MD.svd",
        flash_algorithm="GD32F10x_MD",
        flash_start="08000000",
        flash_length="010000",
    ),
}
KEIL_DEVICE_ALIASES = {
    "STM32F103C8T6": "STM32F103C8",
    "GD32F103C8T6": "GD32F103C8",
}


def keil_device_names() -> tuple[str, ...]:
    return tuple(sorted(KEIL_DEVICE_PROFILES))


def resolve_keil_device_profile(device: str) -> KeilDeviceProfile:
    name = device.strip().upper()
    name = KEIL_DEVICE_ALIASES.get(name, name)
    profile = KEIL_DEVICE_PROFILES.get(name)
    if profile is None:
        supported = ", ".join(keil_device_names())
        raise ValueError(f"unsupported Keil device '{device}'; supported devices: {supported}")
    return profile


@dataclass(frozen=True)
class BuildSelection:
    groups: dict[str, tuple[str, ...]]
    include_dirs: tuple[str, ...]
    defines: tuple[str, ...] = ()
    flash_reserve_bytes: int = 0


@dataclass(frozen=True)
class ComponentBinding:
    """Build-time binding loaded from the central component catalog."""

    compatible: str
    group: str
    header: str
    register: str
    feature: str
    hal: tuple[str, ...]
    requires: tuple[str, ...]
    sources: tuple[Path, ...]


@dataclass(frozen=True)
class HalRootBinding:
    compatible: str
    device: str
    project_prefix: str


@dataclass(frozen=True)
class HalDriverBinding:
    name: str
    compatible: str | None
    sources: tuple[Path, ...]
    project_sources: tuple[str, ...]
    defines: tuple[str, ...]
    managed_defines: tuple[str, ...]


@dataclass(frozen=True)
class HalPlatformBinding:
    name: str
    roots: tuple[HalRootBinding, ...]
    drivers: dict[str, HalDriverBinding]


def _string_list(data: dict[str, Any], key: str) -> tuple[str, ...]:
    value = data.get(key)
    if not isinstance(value, list) or any(not isinstance(item, str) or not item for item in value):
        raise ValueError(f"'{key}' must be an array of non-empty strings")
    if len(value) != len(set(value)):
        raise ValueError(f"'{key}' contains duplicate entries")
    return tuple(value)


def _dts_path(path: Path) -> Path:
    path = path.resolve()
    if path.is_dir():
        candidates = sorted(path.glob("*.dts"))
        if len(candidates) != 1:
            raise ValueError(f"App directory must contain exactly one DTS: {path}")
        path = candidates[0]
    if path.suffix.lower() != ".dts" or not path.is_file():
        raise ValueError(f"DTS configuration does not exist: {path}")
    return path


def _available(node: DtsNode) -> bool:
    current: DtsNode | None = node
    while current is not None:
        status = current.strings("status")
        if status and status[0] not in {"okay", "ok"}:
            return False
        current = current.parent
    return True


def _one_string(node: DtsNode, name: str) -> str:
    values = node.strings(name)
    if len(values) != 1 or not values[0]:
        raise ValueError(f"{node.path}.{name} must contain one non-empty string")
    return values[0]


def component_dts_catalog() -> dict[str, ComponentBinding]:
    """Load and validate the single SDK component build catalog."""
    result: dict[str, ComponentBinding] = {}
    component_root = SDK_ROOT / "component"
    catalog_path = component_root / "common" / "component_catalog.json"
    if not catalog_path.is_file():
        raise ValueError(f"component catalog does not exist: {catalog_path}")
    data = json.loads(catalog_path.read_text(encoding="utf-8"))
    entries = data.get("components")
    if not isinstance(entries, list):
        raise ValueError(f"component catalog must contain a components array: {catalog_path}")
    source_owners: dict[Path, str] = {}
    for entry in entries:
        if not isinstance(entry, dict):
            raise ValueError(f"component catalog entries must be objects: {catalog_path}")
        compatible = entry.get("name")
        if (not isinstance(compatible, str) or
            not IDENTIFIER_PATTERN.fullmatch(compatible) or
            compatible in result):
            raise ValueError(f"invalid or duplicate component name: {compatible}")
        directory = entry.get("directory", compatible)
        if (not isinstance(directory, str) or
            not IDENTIFIER_PATTERN.fullmatch(directory)):
            raise ValueError(
                f"invalid component directory for '{compatible}': {directory}")
        component_directory = (component_root / directory).resolve()
        if (not component_directory.is_dir() or
            component_directory.parent != component_root.resolve()):
            raise ValueError(
                f"component '{compatible}' directory does not exist: "
                f"{component_directory}")
        source_values = _string_list(entry, "sources")
        hal = _string_list(entry, "hal")
        requires_value = entry.get("requires", [])
        if (not isinstance(requires_value, list) or
            any(not isinstance(item, str) or not item
                for item in requires_value) or
            len(requires_value) != len(set(requires_value))):
            raise ValueError(
                f"component '{compatible}' requires must be unique names")
        requires = tuple(requires_value)
        sources: list[Path] = []
        for source_value in source_values:
            source = (component_root / source_value).resolve()
            try:
                source.relative_to(component_root)
            except ValueError as exc:
                raise ValueError(
                    f"component '{compatible}' source escapes component root: "
                    f"{source_value}") from exc
            if not source.is_file() or source.suffix.lower() not in SOURCE_SUFFIXES:
                raise ValueError(
                    f"component '{compatible}' source does not exist: {source}")
            previous_owner = source_owners.get(source)
            if previous_owner is not None:
                raise ValueError(
                    f"component source belongs to both '{previous_owner}' and "
                    f"'{compatible}': {source}")
            source_owners[source] = compatible
            sources.append(source)
        primary_candidates = [
            source for source in sources
            if source.name == f"{compatible}.c"
        ]
        if len(primary_candidates) != 1:
            raise ValueError(
                f"component '{compatible}' requires exactly one {compatible}.c")
        primary = primary_candidates[0]
        if primary.parent != component_directory:
            raise ValueError(
                f"component '{compatible}' primary source must be in "
                f"component/{directory}: {primary}")
        header = primary.with_suffix(".h")
        if not header.is_file():
            raise ValueError(
                f"component '{compatible}' requires matching {compatible}.c/.h")
        result[compatible] = ComponentBinding(
            compatible=compatible,
            group=directory,
            header=header.name,
            register=f"{compatible}_register",
            feature="ARK_DTS_HAS_" + re.sub(
                r"[^A-Za-z0-9]+", "_", compatible).upper(),
            hal=hal,
            requires=requires,
            sources=tuple(sources),
        )
    available_sources = {
        source.resolve()
        for source in component_root.rglob("*.c")
        if source.parent.name != "common"
    }
    unlisted_sources = sorted(available_sources - set(source_owners))
    if unlisted_sources:
        raise ValueError(
            "component catalog does not list source files: " +
            ", ".join(str(source) for source in unlisted_sources))
    for compatible, binding in result.items():
        unknown = sorted(set(binding.requires) - set(result))
        if unknown:
            raise ValueError(
                f"component '{compatible}' requires unknown components: " +
                ", ".join(unknown))
    return result


def hal_platform_catalog() -> dict[str, HalPlatformBinding]:
    """Load and validate the single SDK HAL build catalog."""
    hal_root = SDK_ROOT / "hal"
    catalog_path = hal_root / "common" / "hal_catalog.json"
    if not catalog_path.is_file():
        raise ValueError(f"HAL catalog does not exist: {catalog_path}")
    data = json.loads(catalog_path.read_text(encoding="utf-8"))
    platform_entries = data.get("platforms")
    if not isinstance(platform_entries, list):
        raise ValueError(f"HAL catalog must contain a platforms array: {catalog_path}")

    platforms: dict[str, HalPlatformBinding] = {}
    source_owners: dict[Path, str] = {}
    root_owners: dict[str, str] = {}
    for platform_entry in platform_entries:
        if not isinstance(platform_entry, dict):
            raise ValueError(f"HAL platform entries must be objects: {catalog_path}")
        platform = platform_entry.get("name")
        if (not isinstance(platform, str) or
            not IDENTIFIER_PATTERN.fullmatch(platform) or
            platform in platforms):
            raise ValueError(f"invalid or duplicate HAL platform: {platform}")
        platform_dir = hal_root / platform
        if not platform_dir.is_dir():
            raise ValueError(f"HAL platform directory does not exist: {platform_dir}")

        roots: list[HalRootBinding] = []
        root_entries = platform_entry.get("roots")
        if not isinstance(root_entries, list) or not root_entries:
            raise ValueError(f"HAL platform '{platform}' requires root mappings")
        for root_entry in root_entries:
            if not isinstance(root_entry, dict):
                raise ValueError(f"HAL root entries must be objects: {platform}")
            compatible = root_entry.get("compatible")
            device = root_entry.get("device")
            project_prefix = root_entry.get("project_prefix")
            if (not isinstance(compatible, str) or not compatible or
                not isinstance(device, str) or not device or
                not isinstance(project_prefix, str) or
                not IDENTIFIER_PATTERN.fullmatch(project_prefix)):
                raise ValueError(f"invalid HAL root mapping for platform '{platform}'")
            previous_platform = root_owners.get(compatible)
            if previous_platform is not None:
                raise ValueError(
                    f"HAL root compatible '{compatible}' belongs to both "
                    f"'{previous_platform}' and '{platform}'")
            resolve_keil_device_profile(device)
            root_owners[compatible] = platform
            roots.append(HalRootBinding(compatible, device, project_prefix))

        drivers: dict[str, HalDriverBinding] = {}
        compatible_owners: dict[str, str] = {}
        driver_entries = platform_entry.get("drivers")
        if not isinstance(driver_entries, list) or not driver_entries:
            raise ValueError(f"HAL platform '{platform}' requires drivers")
        for driver_entry in driver_entries:
            if not isinstance(driver_entry, dict):
                raise ValueError(f"HAL driver entries must be objects: {platform}")
            name = driver_entry.get("name")
            if (not isinstance(name, str) or
                not IDENTIFIER_PATTERN.fullmatch(name) or
                name in drivers):
                raise ValueError(f"invalid or duplicate HAL driver in '{platform}': {name}")
            compatible = driver_entry.get("compatible")
            if compatible is not None and (
                not isinstance(compatible, str) or not compatible):
                raise ValueError(f"invalid HAL compatible for '{platform}/{name}'")
            if compatible is not None:
                previous_driver = compatible_owners.get(compatible)
                if previous_driver is not None:
                    raise ValueError(
                        f"HAL compatible '{compatible}' belongs to both "
                        f"'{platform}/{previous_driver}' and '{platform}/{name}'")
                compatible_owners[compatible] = name
            sources: list[Path] = []
            for source_value in _string_list(driver_entry, "sources"):
                source = (hal_root / source_value).resolve()
                try:
                    source.relative_to(hal_root)
                except ValueError as exc:
                    raise ValueError(
                        f"HAL '{platform}/{name}' source escapes HAL root: "
                        f"{source_value}") from exc
                if not source.is_file() or source.suffix.lower() not in SOURCE_SUFFIXES:
                    raise ValueError(
                        f"HAL '{platform}/{name}' source does not exist: {source}")
                previous_owner = source_owners.get(source)
                if previous_owner is not None:
                    raise ValueError(
                        f"HAL source belongs to both '{previous_owner}' and "
                        f"'{platform}/{name}': {source}")
                source_owners[source] = f"{platform}/{name}"
                sources.append(source)
            adapter = (platform_dir / "src" / f"ark_hal_{name}.c").resolve()
            if adapter not in sources:
                raise ValueError(
                    f"HAL '{platform}/{name}' must list its adapter source: {adapter}")
            project_sources = _string_list(driver_entry, "project_sources")
            for project_source in project_sources:
                normalized = project_source.replace("\\", "/")
                project_path = PurePosixPath(normalized)
                if project_path.is_absolute() or ".." in project_path.parts:
                    raise ValueError(
                        f"HAL '{platform}/{name}' project source must stay "
                        f"inside the Cube project: {project_source}")
            drivers[name] = HalDriverBinding(
                name=name,
                compatible=compatible,
                sources=tuple(sources),
                project_sources=project_sources,
                defines=_string_list(driver_entry, "defines"),
                managed_defines=_string_list(driver_entry, "managed_defines"),
            )
        platforms[platform] = HalPlatformBinding(
            name=platform,
            roots=tuple(roots),
            drivers=drivers,
        )

    available_sources = {
        source.resolve()
        for source in hal_root.glob("*/src/*.c")
    } | {
        source.resolve()
        for source in (hal_root / "src").glob("*.c")
    }
    unlisted_sources = sorted(available_sources - set(source_owners))
    if unlisted_sources:
        raise ValueError(
            "HAL catalog does not list source files: " +
            ", ".join(str(source) for source in unlisted_sources))
    return platforms


def _platform_from_dts(document: DtsDocument) -> tuple[str, str, str]:
    root_compatible = set(document.root.strings("compatible"))
    matches: list[tuple[str, str, str]] = []
    for platform in hal_platform_catalog().values():
        for root in platform.roots:
            if root.compatible in root_compatible:
                matches.append((platform.name, root.device, root.project_prefix))
    if len(matches) != 1:
        raise ValueError("DTS root must match exactly one SDK platform/device profile")
    resolve_keil_device_profile(matches[0][1])
    if not IDENTIFIER_PATTERN.fullmatch(matches[0][2]):
        raise ValueError("platform DTS mapping must define a valid project_prefix")
    return matches[0]


def hal_dts_catalog(platform: str) -> dict[str, str]:
    platforms = hal_platform_catalog()
    if platform not in platforms:
        raise ValueError(f"unknown HAL platform: {platform}")
    return {
        driver.compatible: driver.name
        for driver in platforms[platform].drivers.values()
        if driver.compatible is not None
    }


def _named_ioc_candidates(workspace_root: Path, app: str) -> list[Path]:
    """Find only fully unified <container>/<app>/<app>.ioc layouts."""
    candidates: set[Path] = set()
    names = (app, *APP_PROJECT_ALIASES.get(app, ()))
    for name in names:
        direct = workspace_root / name / f"{name}.ioc"
        if direct.is_file():
            candidates.add(direct.resolve())
    for container in workspace_root.iterdir():
        if not container.is_dir() or container.resolve() == SDK_ROOT.resolve():
            continue
        for name in names:
            candidate = container / name / f"{name}.ioc"
            if candidate.is_file():
                candidates.add(candidate.resolve())
    return sorted(candidates)


def _keil_target_names(project: Path) -> tuple[str, ...]:
    try:
        root = ET.parse(project).getroot()
    except (OSError, ET.ParseError) as exc:
        raise ValueError(f"invalid Keil project {project}: {exc}") from exc
    names = tuple(
        value.strip() for value in
        (node.findtext("TargetName") or "" for node in root.findall(".//Target"))
        if value.strip()
    )
    if not names:
        raise ValueError(f"Keil project contains no targets: {project}")
    return names


def _binding_from_ioc(ioc: Path, expected_name: str) -> CubeProjectBinding:
    root = ioc.parent.resolve()
    actual_name = root.name
    accepted_names = (expected_name, *APP_PROJECT_ALIASES.get(expected_name, ()))
    if actual_name not in accepted_names or ioc.stem != actual_name:
        raise ValueError(
            f"Cube directory and IOC must both be named '{expected_name}': {ioc}")
    ioc_values: dict[str, str] = {}
    for line in ioc.read_text(encoding="utf-8", errors="replace").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            ioc_values[key.strip()] = value.strip()
    if ioc_values.get("ProjectManager.ProjectName") != actual_name or \
       ioc_values.get("ProjectManager.ProjectFileName") != f"{actual_name}.ioc":
        raise ValueError(
            f"IOC internal project name/file must both match '{expected_name}'")
    keil_dir = root / "MDK-ARM"
    project = keil_dir / f"{actual_name}.uvprojx"
    if not project.is_file():
        raise ValueError(
            f"CubeMX project must contain MDK-ARM/{expected_name}.uvprojx: {root}")

    targets = _keil_target_names(project)
    if actual_name not in targets:
        raise ValueError(
            f"Keil target must equal IOC project name '{expected_name}'; "
            f"found {targets} in {project}")
    project_root = ET.parse(project).getroot()
    target_node = next(
        node for node in project_root.findall(".//Target")
        if node.findtext("TargetName") == actual_name)
    output_name = (
        target_node.findtext("./TargetOption/TargetCommonOption/OutputName") or
        actual_name).strip()
    if output_name != actual_name:
        raise ValueError(
            f"Keil OutputName must equal '{expected_name}', found '{output_name}'")

    sdk_link = normalize_sdk_path(os.path.relpath(SDK_ROOT, root))
    return CubeProjectBinding(
        root, ioc.resolve(), project.resolve(), actual_name, sdk_link)


def infer_cube_project(
    app: str,
    workspace_root: Path | None = None,
) -> CubeProjectBinding:
    """Resolve one CubeMX/Keil project without storing PC build paths in DTS."""
    root = (workspace_root or SDK_ROOT.parent).resolve()
    expected_name = app
    named = _named_ioc_candidates(root, expected_name)
    if len(named) == 1:
        return _binding_from_ioc(named[0], expected_name)
    if len(named) > 1:
        raise ValueError(
            f"App '{app}' matches multiple {expected_name}.ioc projects: {named}")
    raise ValueError(
        f"no CubeMX project matches App '{app}'; expected the same name: {app}.ioc")


def load_app_config(path: Path) -> AppConfig:
    path = _dts_path(path)
    document = parse_dts(path, SDK_ROOT)
    app = path.parent.name
    if path.stem != app:
        raise ValueError("DTS filename and App directory name must match")
    sdk_root = SDK_ROOT.resolve()
    binding_root = SDK_ROOT.parent
    if path != sdk_root and sdk_root not in path.parents:
        binding_root = path.parent.parent
    platform, device, project_prefix = _platform_from_dts(document)
    if not app.startswith(project_prefix + "_"):
        raise ValueError(
            f"App '{app}' must start with board prefix '{project_prefix}_'")
    binding = infer_cube_project(app, binding_root)
    component_catalog = component_dts_catalog()
    hal_catalog = hal_dts_catalog(platform)
    selected_components: set[str] = set()
    selected_hal: set[str] = set()
    for node in document.walk():
        if not _available(node):
            continue
        for compatible in node.strings("compatible"):
            if compatible in component_catalog:
                component_binding = component_catalog[compatible]
                selected_components.add(component_binding.compatible)
                selected_hal.update(component_binding.hal)
            if compatible in hal_catalog:
                selected_hal.add(hal_catalog[compatible])
    for component in selected_components:
        missing = sorted(
            set(component_catalog[component].requires) - selected_components)
        if missing:
            raise ValueError(
                f"component '{component}' requires enabled components: " +
                ", ".join(missing))
    project_value = os.path.relpath(binding.keil_project, path.parent).replace(os.sep, "/")
    flash_node = next((node for node in document.walk()
                       if "flash_stm32f103" in node.strings("compatible") and _available(node)), None)
    reserve = 0
    if flash_node is not None:
        values = flash_node.cells("ark,reserve-bytes")
        if len(values) != 1 or not isinstance(values[0], int):
            raise ValueError("config Flash requires ark,reserve-bytes")
        reserve = values[0]
    return AppConfig(
        path=path,
        app=app,
        cube_project_root=binding.root,
        ioc_path=binding.ioc,
        platform=platform,
        project_prefix=project_prefix,
        hal=tuple(sorted(selected_hal)),
        components=tuple(sorted(selected_components)),
        keil_project=binding.keil_project,
        keil_project_value=project_value,
        target=binding.target,
        sdk_link=binding.sdk_link,
        device=device,
        reset_and_run=True,
        flash_reserve_bytes=reserve,
    )


def discover_catalog() -> dict[str, Any]:
    platforms = {
        name: sorted(platform.drivers)
        for name, platform in hal_platform_catalog().items()
    }
    components = sorted(component_dts_catalog())
    apps = sorted(
        path.name for path in SDK_ROOT.joinpath("app").iterdir()
        if path.is_dir() and len(list(path.glob("*.dts"))) == 1
    )
    return {"platforms": platforms, "components": components, "apps": apps}


def app_config_path(app: str) -> Path:
    """Return the validated DTS path for an SDK application."""
    if not IDENTIFIER_PATTERN.fullmatch(app):
        raise ValueError(f"invalid app identifier: {app}")
    path = SDK_ROOT / "app" / app / f"{app}.dts"
    if not path.is_file():
        raise ValueError(f"app configuration does not exist: {path}")
    return path.resolve()


def detect_synced_app(project: Path) -> str | None:
    """Detect the single SDK app currently present in a Keil project."""
    project = project.resolve()
    if not project.is_file():
        return None
    root = ET.parse(project).getroot()
    detected: set[str] = set()
    for group in root.findall(".//Group"):
        if (group.findtext("GroupName") or "") != "ARK_SDK/App":
            continue
        for node in group.findall(".//FilePath"):
            parts = (node.text or "").replace("\\", "/").split("/")
            for index, part in enumerate(parts[:-1]):
                if part == "app" and index + 1 < len(parts):
                    detected.add(parts[index + 1])
    return next(iter(detected)) if len(detected) == 1 else None


def hal_driver_binding(platform: str, name: str) -> HalDriverBinding:
    platforms = hal_platform_catalog()
    if platform not in platforms or name not in platforms[platform].drivers:
        raise ValueError(f"unknown HAL driver: {platform}/{name}")
    return platforms[platform].drivers[name]


def managed_hal_defines() -> set[str]:
    defines: set[str] = set()
    for platform in hal_platform_catalog().values():
        for driver in platform.drivers.values():
            defines.update(driver.defines)
            defines.update(driver.managed_defines)
    return defines


def _source_files(directory: Path) -> list[Path]:
    if not directory.is_dir():
        return []
    return sorted(
        path for path in directory.rglob("*")
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES
    )


def _sdk_relative(path: Path) -> str:
    return path.relative_to(SDK_ROOT).as_posix()


def resolve_selection(config: AppConfig) -> BuildSelection:
    catalog = discover_catalog()
    component_catalog = component_dts_catalog()
    if config.platform not in catalog["platforms"]:
        raise ValueError(f"unknown platform '{config.platform}'")
    unknown_hal = sorted(set(config.hal) - set(catalog["platforms"][config.platform]))
    unknown_components = sorted(set(config.components) - set(catalog["components"]))
    if unknown_hal:
        raise ValueError(f"unknown HAL drivers for {config.platform}: {', '.join(unknown_hal)}")
    if unknown_components:
        raise ValueError(f"unknown components: {', '.join(unknown_components)}")

    app_dir = SDK_ROOT / "app" / config.app
    if not app_dir.is_dir():
        raise ValueError(f"app directory does not exist: {app_dir}")
    hal_sources: list[Path] = []
    project_sources: list[str] = []
    defines: list[str] = []
    for driver in config.hal:
        binding = hal_driver_binding(config.platform, driver)
        hal_sources.extend(binding.sources)
        project_sources.extend(f"@project/{path}" for path in binding.project_sources)
        defines.extend(binding.defines)

    component_sources = _source_files(SDK_ROOT / "component" / "common") + [
        source
        for name in config.components
        for source in component_catalog[name].sources
    ]
    app_sources = _source_files(app_dir / "src")
    if not app_sources:
        raise ValueError(f"app '{config.app}' has no source files")

    include_dirs = [SDK_ROOT / "hal" / "include", SDK_ROOT / "component" / "common"]
    platform_include = SDK_ROOT / "hal" / config.platform / "include"
    if platform_include.is_dir():
        include_dirs.append(platform_include)
    for component in config.components:
        component_include = (
            SDK_ROOT / "component" / component_catalog[component].group)
        if component_include not in include_dirs:
            include_dirs.append(component_include)
    app_include = app_dir / "include"
    if app_include.is_dir():
        include_dirs.append(app_include)

    return BuildSelection(
        groups={
            "ARK_SDK/HAL": tuple(_sdk_relative(path) for path in hal_sources),
            "ARK_SDK/Vendor": tuple(project_sources),
            "ARK_SDK/Components": tuple(_sdk_relative(path) for path in component_sources),
            "ARK_SDK/App": tuple(_sdk_relative(path) for path in app_sources),
        },
        include_dirs=tuple(_sdk_relative(path) for path in include_dirs),
        defines=tuple(sorted(set(defines))),
        flash_reserve_bytes=config.flash_reserve_bytes,
    )


def _keil_path(sdk_link: str, suffix: str) -> str:
    return str(PurePosixPath("..") / sdk_link / suffix)


def _file_type(path: str) -> str:
    return SOURCE_SUFFIXES[Path(path).suffix.lower()]


def _keil_source_path(sdk_link: str, source: str) -> str:
    if source.startswith("@project/"):
        return str(PurePosixPath("..") / source.removeprefix("@project/"))
    return _keil_path(sdk_link, source)


_LEGACY_SDK_GROUP = "X" + "Y_SDK"
_LEGACY_SDK_DIRECTORY = "x" + "y_sdk"


def _is_managed_sdk_group(group_name: str) -> bool:
    prefixes = ("ARK_SDK", _LEGACY_SDK_GROUP)
    return any(
        group_name == prefix or group_name.startswith(prefix + "/")
        for prefix in prefixes
    )


def _managed_sdk_path_prefixes(sdk_link: str) -> tuple[str, ...]:
    current = _keil_path(sdk_link, "").rstrip("/").replace("\\", "/") + "/"
    legacy_link = str(PurePosixPath(sdk_link).parent / _LEGACY_SDK_DIRECTORY)
    legacy = _keil_path(legacy_link, "").rstrip("/").replace("\\", "/") + "/"
    return current, legacy


def _sync_keil_options(
    project: Path,
    profile: KeilDeviceProfile,
    reset_and_run: bool,
) -> bool:
    options = project.with_suffix(".uvoptx")
    if not options.is_file():
        raise ValueError(f"Keil options file does not exist: {options}")
    original = options.read_text(encoding="utf-8")

    def update_entry(match: re.Match[str]) -> str:
        name = match.group(2)
        if "-FP0($$Device:" not in name:
            return match.group(0)
        name = re.sub(r"-FF0[^\s]+", f"-FF0{profile.flash_algorithm}", name)
        name = re.sub(r"-FS[0-9A-Fa-f]+", f"-FS{profile.flash_start}", name)
        name = re.sub(r"-FL[0-9A-Fa-f]+", f"-FL{profile.flash_length}", name)
        name = re.sub(
            r"-FP0\(\$\$Device:[^$)]+\$Flash\\[^)]+\)",
            lambda _match: (
                f"-FP0($$Device:{profile.device}$Flash\\"
                f"{profile.flash_algorithm}.FLM)"
            ),
            name,
        )
        option = re.search(r"-FO(\d+)", name)
        if option is None:
            flags = 8 if reset_and_run else 0
            marker = name.find("-FD")
            if marker >= 0:
                name = name[:marker] + f"-FO{flags} " + name[marker:]
            else:
                name += f" -FO{flags}"
        else:
            flags = int(option.group(1))
            flags = (flags | 8) if reset_and_run else (flags & ~8)
            name = name[:option.start()] + f"-FO{flags}" + name[option.end():]
        return match.group(1) + name + match.group(3)

    pattern = r"(<Key>[^<]+</Key>\s*<Name>)(.*?)(</Name>)"
    updated, count = re.subn(pattern, update_entry, original, flags=re.DOTALL)
    if count == 0:
        raise ValueError(f"Keil target driver settings not found in {options}")
    if updated == original:
        return False
    options.write_text(updated, encoding="utf-8", newline="")
    return True


def sync_keil_project(
    project: Path,
    selection: BuildSelection,
    sdk_link: str,
    device: str,
    reset_and_run: bool,
) -> bool:
    project = project.resolve()
    if not project.is_file():
        raise ValueError(f"Keil project does not exist: {project}")
    original = project.read_bytes()
    for sources in selection.groups.values():
        for source in sources:
            source_path = (project.parent / _keil_source_path(sdk_link, source)).resolve()
            if not source_path.is_file():
                raise ValueError(f"selected source does not exist: {source_path}")
    tree = ET.parse(project)
    root = tree.getroot()
    profile = resolve_keil_device_profile(device)
    flash_size = 0x10000 - selection.flash_reserve_bytes
    if (flash_size <= 0) or ((selection.flash_reserve_bytes % 1024) != 0):
        raise ValueError("flash reserve must leave space and use 1-KiB STM32F1 pages")
    cpu = re.sub(
        r"IROM\(0x08000000,0x[0-9A-Fa-f]+\)",
        f"IROM(0x08000000,0x{flash_size:08X})",
        profile.cpu,
    )
    managed_prefixes = _managed_sdk_path_prefixes(sdk_link)
    wanted_includes = [_keil_path(sdk_link, path) for path in selection.include_dirs]
    all_managed_defines = managed_hal_defines()

    device_fields = {
        "Device": profile.device,
        "Vendor": profile.vendor,
        "PackID": profile.pack_id,
        "PackURL": profile.pack_url,
        "Cpu": cpu,
        "FlashDriverDll": profile.flash_driver,
        "RegisterFile": profile.register_file,
        "SFDFile": profile.svd_file,
    }
    common_options = root.findall(".//TargetCommonOption")
    if not common_options:
        raise ValueError(f"TargetCommonOption element not found in {project}")
    for common in common_options:
        for tag, value in device_fields.items():
            node = common.find(tag)
            if node is None:
                raise ValueError(f"{tag} element not found in {project}")
            node.text = value

    for memory in root.findall(".//TargetArmAds//OnChipMemories/*"):
        if ((memory.findtext("Type") == "1") and
            (int(memory.findtext("StartAddress") or "0", 0) == 0x08000000)):
            size = memory.find("Size")
            if size is None:
                raise ValueError(f"IROM size element not found in {project}")
            size.text = f"0x{flash_size:X}"

    for node in root.findall(".//TargetArmAds//IncludePath"):
        old_entries = [entry for entry in (node.text or "").split(";") if entry]
        entries = [
            entry for entry in old_entries
            if not any(
                entry.replace("\\", "/").startswith(prefix)
                for prefix in managed_prefixes
            )
        ]
        entries.extend(path for path in wanted_includes if path not in entries)
        if entries != old_entries:
            node.text = ";".join(entries)

    for node in root.findall(".//TargetArmAds//Define"):
        old_defines = [entry.strip() for entry in (node.text or "").split(",") if entry.strip()]
        defines = [entry for entry in old_defines if entry not in all_managed_defines]
        if defines != old_defines:
            node.text = ",".join(defines)
    for node in root.findall(".//TargetArmAds/Cads//Define"):
        old_defines = [entry.strip() for entry in (node.text or "").split(",") if entry.strip()]
        defines = old_defines + [entry for entry in selection.defines if entry not in old_defines]
        if defines != old_defines:
            node.text = ",".join(defines)

    groups = root.find(".//Groups")
    if groups is None:
        raise ValueError(f"Groups element not found in {project}")
    unmanaged_paths: set[str] = set()
    for group in groups.findall("Group"):
        group_name = group.findtext("GroupName") or ""
        if not _is_managed_sdk_group(group_name):
            unmanaged_paths.update(
                (file.findtext("FilePath") or "").replace("\\", "/")
                for file in group.findall(".//File")
            )
    for group in list(groups.findall("Group")):
        group_name = group.findtext("GroupName") or ""
        if _is_managed_sdk_group(group_name):
            groups.remove(group)

    for group_name, sources in selection.groups.items():
        filtered_sources = tuple(
            source for source in sources
            if _keil_source_path(sdk_link, source).replace("\\", "/") not in unmanaged_paths
        )
        if not filtered_sources:
            continue
        group = ET.SubElement(groups, "Group")
        ET.SubElement(group, "GroupName").text = group_name
        files = ET.SubElement(group, "Files")
        for source in filtered_sources:
            file = ET.SubElement(files, "File")
            ET.SubElement(file, "FileName").text = Path(source).name
            ET.SubElement(file, "FileType").text = _file_type(source)
            ET.SubElement(file, "FilePath").text = _keil_source_path(sdk_link, source)

    ET.indent(tree, space="  ")
    output = io.BytesIO()
    tree.write(output, encoding="utf-8", xml_declaration=True)
    updated = output.getvalue()
    project_changed = updated != original
    if project_changed:
        with tempfile.NamedTemporaryFile("wb", delete=False, dir=project.parent, suffix=".uvprojx.tmp") as temp:
            temp_path = Path(temp.name)
            temp.write(updated)
        os.replace(temp_path, project)
    options_changed = _sync_keil_options(project, profile, reset_and_run)
    return project_changed or options_changed
