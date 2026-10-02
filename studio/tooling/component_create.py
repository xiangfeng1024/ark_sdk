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

"""Create a component from the canonical ARK CREW SDK component template."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import tempfile

from studio.tooling.project_config import component_dts_catalog, hal_platform_catalog


NAME_PATTERN = re.compile(r"^[a-z][a-z0-9]*(?:_[a-z0-9]+)*$")
COMPONENT_TEMPLATE_FILES = ("example.c", "example.h", "example.dts")
GUIDE_TEMPLATE = "组件开发指南模板.md"


def _hal_names() -> set[str]:
    return {
        name
        for platform in hal_platform_catalog().values()
        for name in platform.drivers
    }


def create_plan(sdk_root: Path, name: str, hal: tuple[str, ...] = ()) -> dict[str, object]:
    """Validate inputs and return the paths/content metadata for a new component."""
    sdk_root = sdk_root.resolve()
    component_root = sdk_root / "component"
    template_root = sdk_root / "doc" / "component_template"
    guide_root = sdk_root / "doc" / "components"
    destination = (component_root / name).resolve()
    guide_destination = (
        guide_root / f"{name}_开发指南.md"
    ).resolve()
    catalog_path = component_root / "common" / "component_catalog.json"

    if not component_root.is_dir() or not catalog_path.is_file():
        raise ValueError(f"invalid ARK CREW SDK root: {sdk_root}")
    if not NAME_PATTERN.fullmatch(name):
        raise ValueError("component name must use lowercase snake_case")
    if destination.parent != component_root.resolve():
        raise ValueError("component destination must be a direct child of component/")
    if destination.exists():
        raise ValueError(f"component already exists: {destination}")
    if guide_destination.exists():
        raise ValueError(f"component guide already exists: {guide_destination}")
    missing = [filename for filename in COMPONENT_TEMPLATE_FILES
               if not (template_root / filename).is_file()]
    if not (template_root / GUIDE_TEMPLATE).is_file():
        missing.append(f"doc/component_template/{GUIDE_TEMPLATE}")
    if missing:
        raise ValueError("component template is missing: " + ", ".join(missing))
    if any(not isinstance(item, str) or not item for item in hal):
        raise ValueError("HAL drivers must be non-empty strings")
    if len(hal) != len(set(hal)):
        raise ValueError("HAL drivers must not contain duplicates")
    unknown_hal = sorted(set(hal) - _hal_names())
    if unknown_hal:
        raise ValueError("unknown HAL drivers: " + ", ".join(unknown_hal))
    catalog = component_dts_catalog()
    if name in catalog:
        raise ValueError(f"component catalog already contains '{name}'")
    return {
        "sdk_root": str(sdk_root),
        "name": name,
        "destination": str(destination),
        "guide_destination": str(guide_destination),
        "catalog": str(catalog_path),
        "hal": list(hal),
    }


def _render(text: str, name: str) -> str:
    return text.replace("ARK_COMPONENT_EXAMPLE", "ARK_COMPONENT_" + name.upper()) \
        .replace("example", name) \
        .replace("Example", name.capitalize())


def create_component(plan: dict[str, object]) -> None:
    sdk_root = Path(str(plan["sdk_root"]))
    name = str(plan["name"])
    destination = Path(str(plan["destination"]))
    guide_destination = Path(str(plan["guide_destination"]))
    catalog_path = Path(str(plan["catalog"]))
    hal = list(plan["hal"])
    template_root = sdk_root / "doc" / "component_template"
    component_root = sdk_root / "component"
    temporary = Path(tempfile.mkdtemp(prefix=f".{name}-", dir=component_root))
    published = False
    try:
        for filename in COMPONENT_TEMPLATE_FILES:
            source = template_root / filename
            target_name = filename.replace("example", name)
            target = temporary / target_name
            target.write_text(_render(source.read_text(encoding="utf-8"), name),
                              encoding="utf-8", newline="\n")

        guide_source = template_root / GUIDE_TEMPLATE
        guide_text = _render(guide_source.read_text(encoding="utf-8"), name)

        data = json.loads(catalog_path.read_text(encoding="utf-8"))
        entries = data.get("components")
        if not isinstance(entries, list) or any(
                isinstance(entry, dict) and entry.get("name") == name
                for entry in entries):
            raise ValueError(f"component catalog already contains '{name}'")
        entries.append({
            "name": name,
            "sources": [f"{name}/{name}.c"],
            "hal": hal,
        })
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary.replace(destination)
        published = True
        guide_destination.parent.mkdir(parents=True, exist_ok=True)
        guide_destination.write_text(guide_text, encoding="utf-8", newline="\n")
        catalog_text = json.dumps(data, indent=2) + "\n"
        with tempfile.NamedTemporaryFile(
                "w", encoding="utf-8", newline="\n", delete=False,
                dir=catalog_path.parent, suffix=".catalog.tmp") as catalog_file:
            catalog_temporary = Path(catalog_file.name)
            catalog_file.write(catalog_text)
        os.replace(catalog_temporary, catalog_path)
    except Exception:
        if published and destination.is_dir():
            shutil.rmtree(destination)
        if guide_destination.is_file():
            guide_destination.unlink()
        if "catalog_temporary" in locals() and catalog_temporary.exists():
            catalog_temporary.unlink()
        raise
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-root", type=Path, required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--hal", nargs="*", default=(),
                        help="HAL driver names to record in the catalog")
    parser.add_argument("--check", action="store_true",
                        help="validate and print the creation plan")
    args = parser.parse_args()
    try:
        plan = create_plan(args.sdk_root, args.name, tuple(args.hal))
        if args.check:
            print(json.dumps(plan, ensure_ascii=False, indent=2))
        else:
            create_component(plan)
            print(f"created component: {plan['destination']}")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"error: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
