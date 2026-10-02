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

"""Generate the ARK CREW SDK runtime OF database and build bindings from one DTS."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
from typing import Callable

from studio.tooling.dts_parser import DtsDocument, DtsNode, DtsReference, parse_dts
from studio.tooling.project_config import (
    SDK_ROOT,
    AppConfig,
    component_dts_catalog,
    hal_dts_catalog,
    load_app_config,
)


ProgressCallback = Callable[[int, str, str], None]


def _license_header() -> list[str]:
    license_path = SDK_ROOT / "LICENSE.txt"
    if not license_path.is_file():
        return [
            "/* SPDX-FileCopyrightText: Copyright (c) 2026 Ark Crew. All rights reserved. */",
            "/* SPDX-License-Identifier: LicenseRef-Ark-Crew-NonCommercial-1.0 */",
        ]
    return license_path.read_text(encoding="utf-8").strip().splitlines()

HAL_COUNTS = {"adc": 2, "can": 1, "encoder": 2, "i2c": 2, "pwm": 4, "spi": 3, "uart": 3}
HAL_ARRAYS = {
    "adc": "ark_hal_adc_handles",
    "can": "ark_hal_can_handles",
    "encoder": "ark_hal_encoder_handles",
    "i2c": "ark_hal_i2c_handles",
    "pwm": "ark_hal_pwm_handles",
    "spi": "ark_hal_spi_handles",
    "uart": "ark_hal_uart_handles",
}
HAL_KIND_ENUMS = {
    "gpio": "ARK_OF_HAL_GPIO", "adc": "ARK_OF_HAL_ADC", "can": "ARK_OF_HAL_CAN",
    "encoder": "ARK_OF_HAL_ENCODER", "i2c": "ARK_OF_HAL_I2C", "pwm": "ARK_OF_HAL_PWM",
    "spi": "ARK_OF_HAL_SPI", "uart": "ARK_OF_HAL_UART", "watchdog": "ARK_OF_HAL_WATCHDOG",
    "usb_device": "ARK_OF_HAL_USB_DEVICE",
}
COMPILE_ONLY_PROPERTIES = {
    "ark,cubemx-handle", "ark,cubemx-header", "ark,logical-id", "ark,channel",
}


def _progress(callback: ProgressCallback | None, percent: int, stage: str, message: str) -> None:
    if callback is not None:
        callback(percent, stage, message)


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
    if len(values) != 1:
        raise ValueError(f"{node.path}.{name} must contain one string")
    return values[0]


def _one_u32(node: DtsNode, name: str, default: int | None = None) -> int:
    values = node.cells(name)
    if not values and default is not None:
        return default
    if len(values) != 1 or not isinstance(values[0], int):
        raise ValueError(f"{node.path}.{name} must contain one cell")
    return values[0]


def _validate_references(document: DtsDocument) -> None:
    for node in document.walk():
        if not _available(node):
            continue
        for prop in node.properties.values():
            if prop.kind != "cells":
                continue
            for value in prop.value:
                if not isinstance(value, DtsReference):
                    continue
                provider = document.labels.get(value.label)
                if provider is None:
                    raise ValueError(
                        f"{node.path}.{prop.name} references unknown label &{value.label}")
                if not _available(provider):
                    raise ValueError(
                        f"{node.path}.{prop.name} references disabled provider {provider.path}")


class _Pool:
    def __init__(self) -> None:
        self.data = bytearray()
        self.offsets: dict[bytes, int] = {}

    def add(self, value: bytes, dedupe: bool = False) -> int:
        if dedupe and value in self.offsets:
            return self.offsets[value]
        offset = len(self.data)
        self.data.extend(value)
        if dedupe:
            self.offsets[value] = offset
        return offset


@dataclass
class _FlatProperty:
    name_hash: int
    data_offset: int
    length: int
    type_name: str


@dataclass
class _FlatNode:
    source: DtsNode
    path_hash: int
    parent: int
    first_child: int
    next_sibling: int
    property_index: int
    property_count: int


def _runtime_nodes(document: DtsDocument) -> list[DtsNode]:
    result: list[DtsNode] = []
    for node in document.walk():
        if _available(node):
            result.append(node)
    return result


def _flatten(document: DtsDocument) -> tuple[list[_FlatNode], list[_FlatProperty], bytes]:
    nodes = _runtime_nodes(document)
    if len(nodes) >= 0xFF:
        raise ValueError("runtime DTS exceeds 254 nodes")
    indices = {id(node): index for index, node in enumerate(nodes)}
    data = _Pool()
    properties: list[_FlatProperty] = []
    flat_nodes: list[_FlatNode] = []
    hash_values: dict[str, dict[int, str]] = {
        "path": {},
        "property": {},
    }

    def hashed(text: str, namespace: str) -> int:
        value = 2166136261
        for byte in text.encode("utf-8"):
            value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
        result = (value ^ (value >> 16)) & 0xFFFF
        values = hash_values[namespace]
        previous = values.get(result)
        if previous is not None and previous != text:
            raise ValueError(
                f"DTS {namespace} hash collision: {previous!r} and {text!r}")
        values[result] = text
        return result

    for node in nodes:
        if node.phandle >= 0xFF:
            raise ValueError("runtime DTS phandle exceeds 254")
        path_hash = hashed(node.path, "path")
        included_properties = [
            prop for prop in node.properties.values()
            if prop.name not in COMPILE_ONLY_PROPERTIES and not (
                prop.name == "status" and prop.kind == "strings" and
                tuple(prop.value) in {("okay",), ("ok",)})
        ]
        property_index = len(properties)
        for prop in included_properties:
            prop_name = hashed(prop.name, "property")
            if prop.kind == "bool":
                payload = b""
                type_name = "ARK_OF_PROP_BOOL"
            elif prop.kind == "strings":
                payload = b"".join(value.encode("utf-8") + b"\0" for value in prop.value)
                type_name = "ARK_OF_PROP_STRING"
            elif prop.kind == "cells":
                values: list[int] = []
                for item in prop.value:
                    if isinstance(item, DtsReference):
                        values.append(document.labels[item.label].phandle)
                    else:
                        values.append(item)
                payload = b"".join(int(value & 0xFFFFFFFF).to_bytes(4, "little") for value in values)
                type_name = "ARK_OF_PROP_U32"
            elif prop.kind == "bytes":
                payload = bytes(prop.value)
                type_name = "ARK_OF_PROP_BYTES"
            else:
                raise ValueError(f"unsupported property type: {prop.kind}")
            if len(payload) > 0xFF:
                raise ValueError(
                    f"runtime DTS property {node.path}:{prop.name} exceeds 255 bytes")
            properties.append(_FlatProperty(
                prop_name, data.add(payload, dedupe=True), len(payload), type_name))
        children = [child for child in node.children if id(child) in indices]
        if property_index >= 0xFF or len(included_properties) >= 0xFF:
            raise ValueError("runtime DTS exceeds 254 properties or properties per node")
        parent = 0xFF if node.parent is None else indices[id(node.parent)]
        first_child = 0xFF if not children else indices[id(children[0])]
        sibling = 0xFF
        if node.parent is not None:
            siblings = [child for child in node.parent.children if id(child) in indices]
            position = siblings.index(node)
            if position + 1 < len(siblings):
                sibling = indices[id(siblings[position + 1])]
        flat_nodes.append(_FlatNode(
            node, path_hash, parent, first_child, sibling,
            property_index, len(included_properties)))
    if len(properties) >= 0xFF:
        raise ValueError("runtime DTS exceeds 254 properties")
    return flat_nodes, properties, bytes(data.data)


def _format_bytes(name: str, values: bytes) -> list[str]:
    lines = [f"static const uint8_t {name}[] = {{"]
    if not values:
        lines.append("    0U,")
    for offset in range(0, len(values), 12):
        lines.append("    " + ", ".join(f"0x{item:02X}U" for item in values[offset:offset + 12]) + ",")
    lines.append("};")
    return lines


def _logical_id(kind: str, value: str) -> int:
    if kind == "gpio":
        match = re.fullmatch(r"gpio([a-g])", value)
        return -1 if match is None else ord(match.group(1)) - ord("a")
    match = re.fullmatch(rf"{re.escape(kind)}([1-9][0-9]*)", value)
    return -1 if match is None else int(match.group(1)) - 1


def _validate_uart_stream_topology(document: DtsDocument) -> None:
    streams: dict[str, DtsNode] = {}
    uart_owners: dict[int, DtsNode] = {}
    protocol_names = {"beidou", "bluetooth", "maixcam"}
    wifi_nodes: list[DtsNode] = []

    for node in document.walk():
        if not _available(node):
            continue
        if "stream" in node.strings("compatible"):
            name = _one_string(node, "ark,stream-name")
            if name in streams:
                raise ValueError(
                    f"duplicate UART stream name '{name}': {node.path}")
            parent = node.parent
            while parent is not None and "ark_hal_uart" not in parent.strings("compatible"):
                parent = parent.parent
            if parent is None:
                raise ValueError(f"UART stream has no ark_hal_uart parent: {node.path}")
            logical = _one_string(parent, "ark,logical-id")
            uart_id = _logical_id("uart", logical)
            if uart_id < 0 or uart_id >= HAL_COUNTS["uart"]:
                raise ValueError(f"UART stream parent exceeds UART capacity: {node.path}")
            if uart_id in uart_owners:
                raise ValueError(
                    f"multiple UART streams claim {logical}: {node.path}")
            streams[name] = node
            uart_owners[uart_id] = node

        compatibles = set(node.strings("compatible"))
        if compatibles.intersection(protocol_names):
            parent = node.parent
            if (parent is None) or ("stream" not in parent.strings("compatible")):
                raise ValueError(
                    f"UART protocol must be a child of stream: {node.path}")

        if "wifi_esp_at" in compatibles:
            wifi_nodes.append(node)

    for node in wifi_nodes:
        parent = node.parent
        if (parent is not None) and ("stream" in parent.strings("compatible")):
            stream_name = _one_string(parent, "ark,stream-name")
        else:
            stream_name = _one_string(node, "ark,stream-name")
        if stream_name not in streams:
            raise ValueError(
                f"wifi_esp_at references unknown stream '{stream_name}': {node.path}")


def _validate_component_classes(document: DtsDocument) -> None:
    class_backends = {
        "motor": {"motor_tb6612", "motor_can"},
        "flash": {"flash_stm32f103", "flash_w25q16"},
        "wifi": {"wifi_esp_at"},
    }

    for node in document.walk():
        if not _available(node):
            continue
        compatibles = set(node.strings("compatible"))
        for class_name, backend_names in class_backends.items():
            selected = compatibles.intersection(backend_names)
            if selected:
                parent = node.parent
                if (parent is None or
                        class_name not in parent.strings("compatible")):
                    raise ValueError(
                        f"{next(iter(selected))} must be a child of "
                        f"{class_name}: {node.path}")
            if class_name in compatibles:
                enabled_backends = [
                    child for child in node.children
                    if _available(child) and
                    set(child.strings("compatible")).intersection(backend_names)
                ]
                selected_names = [
                    next(iter(
                        set(child.strings("compatible")).intersection(
                            backend_names)))
                    for child in enabled_backends
                ]
                if len(selected_names) != len(set(selected_names)):
                    raise ValueError(
                        f"{class_name} contains duplicate backend types: "
                        f"{node.path}")
                minimum = 1
                maximum = 1 if class_name in {"motor", "wifi"} else len(backend_names)
                if not (minimum <= len(enabled_backends) <= maximum):
                    raise ValueError(
                        f"{class_name} requires "
                        f"{minimum}..{maximum} enabled backend children: "
                        f"{node.path}")


def _render(config: AppConfig, document: DtsDocument) -> tuple[str, str]:
    nodes = _runtime_nodes(document)
    node_indices = {id(node): index for index, node in enumerate(nodes)}
    component_catalog = component_dts_catalog()
    hal_catalog = hal_dts_catalog(config.platform)
    registrations: dict[str, str] = {}
    selected_features: set[str] = set()
    headers: set[str] = set()
    handles: dict[str, dict[int, tuple[str, str, DtsNode]]] = {
        kind: {} for kind in HAL_COUNTS
    }
    hal_refs: list[tuple[int, str, int]] = []
    watchdog: tuple[str, str] | None = None
    usb_device: tuple[str, str] | None = None
    pwm_channels: dict[int, int] = {}
    hal_ids: set[tuple[str, int]] = set()
    _validate_references(document)
    _validate_uart_stream_topology(document)
    _validate_component_classes(document)
    for node in document.walk():
        if not _available(node):
            continue
        for compatible in node.strings("compatible"):
            match_info = component_catalog.get(compatible)
            if match_info is not None:
                registrations[match_info.register] = match_info.header
                headers.add(match_info.header)
                selected_features.add(match_info.feature)
            kind = hal_catalog.get(compatible)
            if (node.path.startswith("/sys/") and match_info is None and
                    kind is None):
                raise ValueError(
                    f"enabled device compatible has no SDK match: {node.path}: {compatible}")
            if kind is None:
                continue
            if kind in {"time"}:
                continue
            logical = _one_string(node, "ark,logical-id")
            identifier = _logical_id(kind, logical)
            key = (kind, identifier)
            if key in hal_ids:
                raise ValueError(f"duplicate {kind} logical resource: {logical}")
            hal_ids.add(key)
            if kind == "gpio":
                if identifier < 0:
                    raise ValueError(f"invalid GPIO logical id: {logical}")
                hal_refs.append((node_indices[id(node)], HAL_KIND_ENUMS[kind], identifier))
                continue
            handle = _one_string(node, "ark,cubemx-handle")
            header = _one_string(node, "ark,cubemx-header")
            headers.add(header)
            if kind == "watchdog":
                watchdog = (handle, header)
            elif kind == "usb_device":
                usb_device = (handle, header)
            else:
                if identifier < 0 or identifier >= HAL_COUNTS[kind]:
                    raise ValueError(f"{logical} exceeds {kind} capacity")
                handles[kind][identifier] = (handle, header, node)
                hal_refs.append((node_indices[id(node)], HAL_KIND_ENUMS[kind], identifier))
                if kind == "pwm":
                    pwm_channels[identifier] = _one_u32(node, "ark,channel")
    display = document.find_path("/software/display")
    dirty_refresh = bool(display and _one_u32(display, "ark,dirty-refresh", 1))
    ws_node = next((node for node in document.walk()
                    if "ws2812b" in node.strings("compatible") and _available(node)), None)
    ws_transport = _one_string(ws_node, "ark,transport") if ws_node else ""
    macros = {
        binding.feature: binding.feature in selected_features
        for binding in component_catalog.values()
    }
    macros.update({
        "ARK_DTS_OLED_DIRTY_REFRESH": dirty_refresh,
        "ARK_DTS_WS2812B_PWM_ENABLED": ws_transport == "pwm-dma",
        "ARK_DTS_WS2812B_SOFTWARE_ENABLED": ws_transport == "software",
    })
    header_lines = [
        *_license_header(),
        "/* Generated by ark_dts.py. Do not edit. */",
        "#ifndef ARK_DTS_GENERATED_H", "#define ARK_DTS_GENERATED_H", "",
        "#include <stdbool.h>", "", f'#define ARK_DTS_APP_NAME "{config.app}"',
    ]
    header_lines.extend(f"#define {name} {1 if enabled else 0}" for name, enabled in macros.items())
    header_lines.extend(("", "bool ark_dts_register_components(void);", "", "#endif", ""))

    flat_nodes, properties, value_data = _flatten(document)
    source_lines = [
        *_license_header(),
        "/* Generated by ark_dts.py. Do not edit. */",
        '#include "ark_dts_generated.h"', "", "#include <stddef.h>", "#include <stdint.h>",
        '#include "ark_dts.h"', '#include "ark_hal_bindings.h"',
    ]
    source_lines.extend(f'#include "{header}"' for header in sorted(headers))
    source_lines.append("")
    for kind, count in HAL_COUNTS.items():
        source_lines.append(f"void *const {HAL_ARRAYS[kind]}[{count}] = {{")
        for index in range(count):
            source_lines.append(f"    {('&' + handles[kind][index][0]) if index in handles[kind] else 'NULL'},")
        source_lines.extend(("};", ""))
    source_lines.append(f"void *const ark_hal_watchdog_handle = {('&' + watchdog[0]) if watchdog else 'NULL'};")
    source_lines.append(f"void *const ark_hal_usb_device_handle = {('&' + usb_device[0]) if usb_device else 'NULL'};")
    source_lines.extend(("", "const uint32_t ark_hal_pwm_channels[4] = {"))
    for index in range(4):
        channel = pwm_channels.get(index)
        source_lines.append(f"    {('TIM_CHANNEL_' + str(channel)) if channel else '0U'},")
    source_lines.extend((
        "};", "",
        "const ark_hal_soft_i2c_config_t ark_hal_soft_i2c_configs[1] = {{",
        "    .scl = {.port = ARK_HAL_GPIO_PORT_A, .pin = 0U, .active_level = 0U},",
        "    .sda = {.port = ARK_HAL_GPIO_PORT_A, .pin = 0U, .active_level = 0U},",
        "    .delay_us = 0U,",
        "    .deinit_hardware_i2c = false, .hardware_i2c_id = ARK_HAL_I2C_1,",
        "}};", ""))
    source_lines.extend(_format_bytes("ark_dts_data", value_data))
    source_lines.extend(("", "static const ark_of_property_t ark_dts_properties[] = {"))
    if not properties:
        source_lines.append("    {0},")
    for prop in properties:
        source_lines.append(
            f"    {{{prop.name_hash}U, {prop.data_offset}U, {prop.length}U, {prop.type_name}}},")
    source_lines.extend(("};", "", "static const ark_of_node_t ark_dts_nodes[] = {"))
    for node in flat_nodes:
        source_lines.append(
            "    {" +
            f"{node.path_hash}U, {node.parent}U, {node.first_child}U, " +
            f"{node.next_sibling}U, {node.property_index}U, {node.property_count}U, " +
            f"{node.source.phandle}U" + "},")
    source_lines.extend(("};", "", "static const ark_of_hal_ref_t ark_dts_hal_refs[] = {"))
    if not hal_refs:
        source_lines.append("    {0},")
    for node_index, kind_enum, identifier in hal_refs:
        source_lines.append(f"    {{{node_index}U, {kind_enum}, {identifier}U}},")
    source_lines.extend((
        "};", "", "const ark_of_blob_t ark_of_blob = {",
        "    ark_dts_nodes, sizeof(ark_dts_nodes) / sizeof(ark_dts_nodes[0]),",
        "    ark_dts_properties, sizeof(ark_dts_properties) / sizeof(ark_dts_properties[0]),",
        "    ark_dts_data,",
        "    ark_dts_hal_refs, sizeof(ark_dts_hal_refs) / sizeof(ark_dts_hal_refs[0]),",
        "};", "", "bool ark_dts_register_components(void)", "{", "    bool ok = true;",
    ))
    for register in registrations:
        source_lines.append(f"    ok = {register}() && ok;")
    source_lines.extend(("    return ok;", "}", ""))
    return "\n".join(header_lines), "\n".join(source_lines)


def generated_paths(config_path: Path) -> tuple[Path, Path]:
    config = load_app_config(config_path)
    return (config.path.parent / "include" / "ark_dts_generated.h",
            config.path.parent / "src" / "ark_dts_generated.c")


def generate_for_app(
    config_path: Path,
    check: bool = False,
    progress: ProgressCallback | None = None,
) -> tuple[Path, Path, bool]:
    config = load_app_config(config_path)
    _progress(progress, 15, "load", "DTS与组件源码目录读取完成")
    document = parse_dts(config.path, SDK_ROOT)
    _progress(progress, 35, "parse", "DTS节点、标签和phandle解析完成")
    header_text, source_text = _render(config, document)
    _progress(progress, 60, "validate", "compatible、HAL资源和组件源码校验完成")
    header, source = generated_paths(config.path)
    _progress(progress, 82, "render", "OF数据库和句柄映射渲染完成")
    stale = ((not header.is_file()) or (header.read_text(encoding="utf-8") != header_text) or
             (not source.is_file()) or (source.read_text(encoding="utf-8") != source_text))
    if not check and stale:
        header.parent.mkdir(parents=True, exist_ok=True)
        source.parent.mkdir(parents=True, exist_ok=True)
        header.write_text(header_text, encoding="utf-8", newline="\n")
        source.write_text(source_text, encoding="utf-8", newline="\n")
    _progress(progress, 92, "write", "生成文件比较或写入完成")
    return header, source, stale


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate C OF data from one ARK CREW SDK DTS")
    parser.add_argument("app", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        header, source, stale = generate_for_app(args.app, check=args.check)
        if args.check:
            print(f"valid: {load_app_config(args.app).path}")
            print("generated outputs: " + ("stale" if stale else "current"))
            return 1 if stale else 0
        print(f"generated: {header}")
        print(f"generated: {source}")
        return 0
    except ValueError as exc:
        print(f"error: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
