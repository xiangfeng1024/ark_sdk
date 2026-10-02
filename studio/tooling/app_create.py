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

"""Create one unified ARK CREW SDK App and its same-named CubeMX/Keil project."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import shutil
import tempfile

from studio.tooling.cube_project_tool import (
    _clone_ignore,
    _cube_ioc,
    _keil_project,
    _portable_relative,
    _rename_project_files,
    _rewrite_keil_sdk_path,
)
from studio.tooling.project_config import (
    detect_synced_app,
    hal_platform_catalog,
    load_app_config,
    resolve_selection,
    sync_keil_project,
)
from studio.tooling.ark_dts import generate_for_app


APP_NAME_PATTERN = re.compile(r"^[a-z0-9][a-z0-9_]*$")


def _emit_progress(percent: int, message: str) -> None:
    print(f"[ARK_PROGRESS] {percent} {message}", flush=True)


def _require_child(path: Path, parent: Path, description: str) -> Path:
    resolved = path.resolve()
    parent_resolved = parent.resolve()
    if resolved.parent != parent_resolved:
        raise ValueError(f"{description} must be a direct child of {parent_resolved}")
    return resolved


def _platform_compatible(config) -> str:
    platform = hal_platform_catalog().get(config.platform)
    if platform is None:
        raise ValueError(f"unknown template platform: {config.platform}")
    matches = [
        item.compatible for item in platform.roots
        if item.device == config.device and item.project_prefix == config.project_prefix
    ]
    if len(matches) != 1:
        raise ValueError(
            f"template platform must map to one root compatible, found {matches}")
    return matches[0]


def create_plan(
    sdk_root: Path,
    template_config: Path,
    source: Path,
    cube_parent: Path,
    name: str,
) -> dict[str, str]:
    sdk_root = sdk_root.resolve()
    if not (sdk_root / "app").is_dir() or not (sdk_root / "component").is_dir():
        raise ValueError(f"invalid ARK CREW SDK root: {sdk_root}")
    if not APP_NAME_PATTERN.fullmatch(name) or "_" not in name:
        raise ValueError(
            "App name must use lowercase <board_prefix>_<product> characters")

    template = load_app_config(template_config)
    expected_prefix = f"{template.project_prefix}_"
    if not name.startswith(expected_prefix):
        raise ValueError(
            f"App name must start with the template board prefix '{expected_prefix}'")

    source = source.resolve()
    cube_parent = cube_parent.resolve()
    if not source.is_dir():
        raise ValueError(f"CubeMX template does not exist: {source}")
    if not cube_parent.is_dir():
        raise ValueError(f"CubeMX destination parent does not exist: {cube_parent}")
    source_ioc = _cube_ioc(source)
    source_name = source_ioc.stem
    if not source_name.startswith(expected_prefix):
        raise ValueError(
            f"CubeMX template '{source_name}' does not match board prefix "
            f"'{template.project_prefix}'")
    _keil_project(source, source_name)

    app_root = (sdk_root / "app").resolve()
    app_destination = _require_child(app_root / name, app_root, "App destination")
    cube_destination = _require_child(
        cube_parent / name, cube_parent, "CubeMX destination")
    if app_destination.exists():
        raise ValueError(f"App already exists: {app_destination}")
    if cube_destination.exists():
        raise ValueError(f"CubeMX project already exists: {cube_destination}")
    if source == cube_destination or source in cube_destination.parents:
        raise ValueError("CubeMX destination cannot be the template or its child")

    return {
        "name": name,
        "sdk_root": str(sdk_root),
        "template_config": str(template.path),
        "source": str(source),
        "source_name": source_name,
        "app_destination": str(app_destination),
        "cube_destination": str(cube_destination),
        "platform_compatible": _platform_compatible(template),
    }


def _source_license_header(sdk_root: Path) -> str:
    license_path = sdk_root / "LICENSE.txt"
    if not license_path.is_file():
        raise FileNotFoundError(f"SDK license does not exist: {license_path}")
    return license_path.read_text(encoding="utf-8").strip() + "\n\n"


def _write_app_skeleton(
    sdk_root: Path,
    destination: Path,
    name: str,
    compatible: str,
) -> None:
    include = destination / "include"
    source = destination / "src"
    include.mkdir(parents=True)
    source.mkdir(parents=True)
    license_header = _source_license_header(sdk_root)

    dts_text = f'''/dts-v1/;

/ {{
    model = "ARK CREW {name}";
    compatible = "{name}", "{compatible}";

    sys {{
        compatible = "simple-bus";
        #address-cells = <1>;
        #size-cells = <0>;
    }};

    software {{
        status = "okay";
    }};
}};
'''
    header_text = license_header + '''#ifndef APP_MAIN_H
#define APP_MAIN_H

#include <stdbool.h>

bool components_init(void);
void appStartTask(void *argument);

#endif /* APP_MAIN_H */
'''
    source_text = license_header + '''#include "app_main.h"

#include "FreeRTOS.h"
#include "task.h"
#include "ark_dts_generated.h"

bool components_init(void)
{
    return ark_dts_register_components();
}

void appStartTask(void *argument)
{
    if (!components_init()) {
        vTaskDelete(NULL);
        return;
    }

    vTaskDelete(NULL);
}
'''
    requirements_text = f'''# {name} 项目需求

## 当前状态

该 App 由 ARK CREW Studio“新建 App”工具生成，目前只包含最小 DTS、组件注册入口和
`appStartTask()`。请先在同名 CubeMX 工程中配置所需外设并重新生成，再向 DTS 添加
`/sys` 硬件/组件节点、向 `/software` 添加业务参数，并在 App `src` 中实现产品逻辑。

App、DTS、CubeMX 目录、IOC、Keil 工程和 Keil Target 必须始终保持 `{name}` 同名。
'''

    (destination / f"{name}.dts").write_text(
        dts_text, encoding="utf-8", newline="\n")
    (include / "app_main.h").write_text(
        header_text, encoding="utf-8", newline="\n")
    (source / "app_main.c").write_text(
        source_text, encoding="utf-8", newline="\n")
    (destination / "PROJECT_REQUIREMENTS.md").write_text(
        requirements_text, encoding="utf-8", newline="\n")


def create_app(plan: dict[str, str]) -> None:
    sdk_root = Path(plan["sdk_root"])
    source = Path(plan["source"])
    source_name = plan["source_name"]
    name = plan["name"]
    app_destination = Path(plan["app_destination"])
    cube_destination = Path(plan["cube_destination"])
    app_root = (sdk_root / "app").resolve()
    cube_parent = cube_destination.parent.resolve()

    app_temporary = Path(tempfile.mkdtemp(prefix=f".{name}.app-", dir=app_root))
    cube_temporary = Path(tempfile.mkdtemp(prefix=f".{name}.cube-", dir=cube_parent))
    app_temporary.rmdir()
    cube_temporary.rmdir()
    app_published = False
    cube_published = False
    try:
        _emit_progress(15, "复制CubeMX模板")
        shutil.copytree(
            source,
            cube_temporary,
            ignore=_clone_ignore(source_name),
            copy_function=shutil.copy2,
        )
        _emit_progress(38, "统一IOC、Keil工程和Target名称")
        project_file = _rename_project_files(
            cube_temporary, source_name, name)
        sdk_reference = _portable_relative(sdk_root, cube_destination)
        _rewrite_keil_sdk_path(project_file, sdk_reference)

        _emit_progress(52, "创建最小DTS App源码")
        _write_app_skeleton(
            sdk_root, app_temporary, name, plan["platform_compatible"])

        _emit_progress(64, "发布同名App与CubeMX工程")
        cube_temporary.replace(cube_destination)
        cube_published = True
        app_temporary.replace(app_destination)
        app_published = True

        dts = app_destination / f"{name}.dts"
        _emit_progress(74, "生成DTS运行时OF文件")
        generate_for_app(dts)
        config = load_app_config(dts)
        selection = resolve_selection(config)

        _emit_progress(86, "同步新App到Keil工程")
        sync_keil_project(
            config.keil_project,
            selection,
            config.sdk_link,
            config.device,
            config.reset_and_run,
        )
        _emit_progress(94, "验证新App、DTS和Keil同步结果")
        _, _, stale = generate_for_app(dts, check=True)
        if stale:
            raise ValueError("new App DTS generated files are stale")
        if detect_synced_app(config.keil_project) != name:
            raise ValueError("new Keil project did not synchronize the new App")
        if not (app_destination / "src" / "ark_dts_generated.c").is_file():
            raise ValueError("new App generated source is missing")

        print(f"created App: {app_destination}")
        print(f"created CubeMX project: {cube_destination}")
        print(f"DTS: {dts}")
        print(f"Keil: {config.keil_project}")
        print("next: configure peripherals in CubeMX, regenerate, then run DTS check and Keil sync")
        _emit_progress(100, "新App创建并同步完成")
    except Exception:
        if app_published and app_destination.parent.resolve() == app_root:
            shutil.rmtree(app_destination)
        elif app_temporary.exists() and app_temporary.parent.resolve() == app_root:
            shutil.rmtree(app_temporary)
        if cube_published and cube_destination.parent.resolve() == cube_parent:
            shutil.rmtree(cube_destination)
        elif cube_temporary.exists() and cube_temporary.parent.resolve() == cube_parent:
            shutil.rmtree(cube_temporary)
        raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-root", type=Path, required=True)
    parser.add_argument("--template-config", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--cube-parent", type=Path, required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        _emit_progress(5, "检查SDK、模板和新App参数")
        plan = create_plan(
            args.sdk_root,
            args.template_config,
            args.source,
            args.cube_parent,
            args.name,
        )
        _emit_progress(10, "新App目标路径和版型匹配检查完成")
        if args.check:
            print(json.dumps(plan, ensure_ascii=False, indent=2))
            _emit_progress(100, "新App工具自检通过")
            return 0
        create_app(plan)
        return 0
    except (OSError, ValueError) as exc:
        print(f"error: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
