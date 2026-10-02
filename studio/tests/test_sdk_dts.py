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

from __future__ import annotations

import json
from pathlib import Path
import re
import sys
import tempfile
import unittest
from unittest.mock import patch


SCRIPT_ROOT = Path(__file__).resolve().parents[1]
SDK_ROOT = SCRIPT_ROOT.parent
sys.path.insert(0, str(SCRIPT_ROOT))

from studio.tooling.project_config import (
    CubeProjectBinding,
    component_dts_catalog,
    hal_platform_catalog,
    load_app_config,
)
from studio.tooling.ark_dts import (
    _validate_component_classes,
    _validate_uart_stream_topology,
    generate_for_app,
)


class SdkDtsTests(unittest.TestCase):
    def setUp(self) -> None:
        self.dts = (SDK_ROOT / "app" / "c8t6_microcar_soil" /
                    "c8t6_microcar_soil.dts")

    def test_microcar_soil_is_discovered_from_dts(self) -> None:
        config = load_app_config(self.dts)
        self.assertEqual(config.app, "c8t6_microcar_soil")
        self.assertEqual(config.platform, "stm32f1")
        self.assertIn("oled", config.components)
        self.assertIn("motor", config.components)
        self.assertIn("motor_tb6612", config.components)
        self.assertIn("encoder", config.components)
        self.assertIn("stream", config.components)
        self.assertIn("beidou", config.components)
        self.assertIn("bluetooth", config.components)
        self.assertIn("maixcam", config.components)
        self.assertNotIn("uart_device", config.components)
        self.assertIn("i2c", config.hal)
        self.assertEqual(config.flash_reserve_bytes, 1024)

    def test_ark_net_selects_network_components(self) -> None:
        dts = SDK_ROOT / "app" / "c8t6_ark_net" / "c8t6_ark_net.dts"
        config = load_app_config(dts)

        self.assertEqual(config.app, "c8t6_ark_net")
        self.assertIn("wifi", config.components)
        self.assertIn("wifi_esp_at", config.components)
        self.assertIn("ark_net", config.components)
        self.assertIn("json", config.components)
        self.assertIn("stream", config.components)
        self.assertIn("uart", config.hal)

    def test_components_are_loaded_from_one_central_catalog(self) -> None:
        catalog = component_dts_catalog()

        self.assertFalse(any((SDK_ROOT / "component").glob("*/component.json")))
        self.assertEqual(catalog["dht11"].register, "dht11_register")
        self.assertEqual(catalog["motor_tb6612"].group, "motor")
        self.assertEqual(
            [path.name for path in catalog["oled"].sources],
            ["oled.c", "ssd1306.c"],
        )
        self.assertIn("pwm", catalog["motor_tb6612"].hal)
        listed_sources = {
            path.resolve()
            for binding in catalog.values()
            for path in binding.sources
        }
        actual_sources = {
            path.resolve()
            for path in (SDK_ROOT / "component").rglob("*.c")
            if path.parent.name != "common"
        }
        self.assertEqual(listed_sources, actual_sources)

    def test_hal_is_loaded_from_one_central_catalog(self) -> None:
        platforms = hal_platform_catalog()
        stm32f1 = platforms["stm32f1"]

        self.assertFalse(any((SDK_ROOT / "hal" / "stm32f1").glob("*.json")))
        self.assertEqual(stm32f1.drivers["gpio"].compatible, "ark_hal_gpio")
        self.assertEqual(
            [path.name for path in stm32f1.drivers["uart"].sources],
            ["ark_hal_uart.c", "ark_uart_manage.c"],
        )
        listed_sources = {
            path.resolve()
            for platform in platforms.values()
            for driver in platform.drivers.values()
            for path in driver.sources
        }
        actual_sources = {
            path.resolve()
            for path in (SDK_ROOT / "hal").glob("*/src/*.c")
        } | {
            path.resolve()
            for path in (SDK_ROOT / "hal" / "src").glob("*.c")
        }
        self.assertEqual(listed_sources, actual_sources)

    def test_check_reports_real_phases_and_build_metadata_is_not_runtime_data(self) -> None:
        events: list[tuple[int, str, str]] = []
        header, source, stale = generate_for_app(
            self.dts, check=True,
            progress=lambda percent, stage, message: events.append((percent, stage, message)))
        self.assertFalse(stale)
        self.assertEqual([item[0] for item in events], [15, 35, 60, 82, 92])
        generated = source.read_text(encoding="utf-8")
        self.assertNotIn("ark,keil-project", generated)
        self.assertIn("ark_dts_register_components", generated)
        self.assertTrue(header.is_file())

    def test_uart_stream_topology_rejects_duplicate_stream_names(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            dts = root / "topology.dts"
            dts.write_text('''/dts-v1/;
/ {
    sys {
        uart1: uart1 {
            compatible = "ark_hal_uart";
            ark,logical-id = "uart1";
            stream_a: stream { compatible = "stream"; ark,stream-name = "same"; };
        };
        uart2: uart2 {
            compatible = "ark_hal_uart";
            ark,logical-id = "uart2";
            stream_b: stream { compatible = "stream"; ark,stream-name = "same"; };
        };
    };
};
''', encoding="utf-8")
            from studio.tooling.dts_parser import parse_dts

            document = parse_dts(dts, root)
            with self.assertRaisesRegex(ValueError, "duplicate UART stream name"):
                _validate_uart_stream_topology(document)

    def test_component_class_rejects_orphan_and_multiple_backends(self) -> None:
        from studio.tooling.dts_parser import parse_dts

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            orphan = root / "orphan.dts"
            orphan.write_text('''/dts-v1/;
/ { motor_tb6612 { compatible = "motor_tb6612"; }; };
''', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "must be a child of motor"):
                _validate_component_classes(parse_dts(orphan, root))

            multiple = root / "multiple.dts"
            multiple.write_text('''/dts-v1/;
/ {
    motor {
        compatible = "motor";
        a { compatible = "motor_tb6612"; };
        b { compatible = "motor_can"; };
    };
};
''', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "requires 1..1"):
                _validate_component_classes(parse_dts(multiple, root))

    def _fixture(self, root: Path, device_status: str, compatible: str = "led") -> Path:
        app = root / "c8t6_demo"
        app.mkdir()
        cube = root / "c8t6_demo"
        keil = cube / "MDK-ARM"
        keil.mkdir(parents=True)
        (cube / "c8t6_demo.ioc").write_text(
            "ProjectManager.ProjectName=c8t6_demo\n", encoding="utf-8")
        (keil / "c8t6_demo.uvprojx").write_text(
            "<Project><Targets><Target><TargetName>c8t6_demo</TargetName>"
            "</Target></Targets></Project>", encoding="utf-8")
        dts = app / "c8t6_demo.dts"
        dts.write_text(f'''/dts-v1/;
/ {{
    compatible = "c8t6_demo", "stm32f103c8";
    sys {{
        compatible = "simple-bus";
        gpioa: gpio@0 {{
            compatible = "ark_hal_gpio";
            gpio-controller;
            #gpio-cells = <2>;
            ark,logical-id = "gpioa";
        }};
        leds {{
            compatible = "{compatible}";
            status = "{device_status}";
            led@0 {{ reg = <0>; gpios = <&gpioa 13 1>; }};
            led@1 {{ reg = <5>; gpios = <&gpioa 12 0>; }};
        }};
    }};
    software {{}};
}};
''', encoding="utf-8")
        binding = CubeProjectBinding(
            cube, cube / "c8t6_demo.ioc", keil / "c8t6_demo.uvprojx",
            "c8t6_demo", "../ark_sdk")
        return dts, binding

    def test_disabled_component_is_not_selected_or_generated(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            dts, binding = self._fixture(Path(temporary), "disabled")
            with patch("studio.tooling.project_config.infer_cube_project", return_value=binding):
                config = load_app_config(dts)
                self.assertNotIn("led", config.components)
                _header, source, _changed = generate_for_app(dts)
            generated = source.read_text(encoding="utf-8")
            self.assertNotIn("led_register", generated)

    def test_unknown_enabled_device_compatible_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            dts, binding = self._fixture(
                Path(temporary), "okay", "ark,unknown-device")
            with patch("studio.tooling.project_config.infer_cube_project", return_value=binding):
                with self.assertRaisesRegex(ValueError, "no SDK match"):
                    generate_for_app(dts)


class ArkMigrationTests(unittest.TestCase):
    _SKIP_PARTS = {
        ".git", "node_modules", "dist", "build", "__pycache__",
        ".pytest_cache", "staging", "tmp", "temp", "log", "web", "out",
    }
    _TEXT_SUFFIXES = {
        ".c", ".h", ".py", ".dts", ".md", ".json", ".jsonc",
        ".ts", ".tsx", ".js", ".mjs", ".html", ".yml", ".yaml",
        ".ps1", ".txt",
    }

    @classmethod
    def _repository_files(cls, suffixes: set[str]):
        for path in SDK_ROOT.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in suffixes:
                continue
            relative = path.relative_to(SDK_ROOT)
            if any(part in cls._SKIP_PARTS for part in relative.parts):
                continue
            yield path

    def test_old_public_brand_identifiers_are_absent(self) -> None:
        old_word = "xiao" + "yan"
        allowed_domain = "gitea." + old_word + "fenxiangwu.dpdns.org"
        allowed_external_project = "c8t6_" + old_word + "_net"
        forbidden = ("xy" + "_", "X" + "Y" + "_", old_word,
                     old_word.upper(), "xy" + ",")
        failures = []

        for path in self._repository_files(self._TEXT_SUFFIXES):
            text = path.read_text(encoding="utf-8", errors="ignore")
            text = text.replace(allowed_domain, "gitea.example")
            text = text.replace(allowed_external_project, "external_cube_project")
            old_initialism = re.search(r"\b" + "X" + "Y" + r"\b", text)
            if any(token in text for token in forbidden) or old_initialism:
                failures.append(str(path.relative_to(SDK_ROOT)))

        self.assertEqual(failures, [])

    def test_owned_sources_have_ark_license(self) -> None:
        marker = "SPDX-License-Identifier: LicenseRef-Ark-Crew-NonCommercial-1.0"
        failures = []

        for path in self._repository_files({".c", ".h", ".py"}):
            relative = path.relative_to(SDK_ROOT).as_posix()
            if relative in {"component/json/cJSON.c", "component/json/cJSON.h"}:
                continue
            head = path.read_text(encoding="utf-8", errors="ignore")[:8192]
            if marker not in head:
                failures.append(relative)

        self.assertEqual(failures, [])

    def test_component_catalog_matches_independent_directories(self) -> None:
        catalog_path = SDK_ROOT / "component" / "common" / "component_catalog.json"
        catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
        owners: dict[str, str] = {}

        for entry in catalog["components"]:
            name = entry["name"]
            directory_name = entry.get("directory", name)
            directory = SDK_ROOT / "component" / directory_name
            main_source = directory / f"{name}.c"
            public_header = directory / f"{name}.h"
            guide = (
                SDK_ROOT / "doc" / "components" /
                f"{name}_开发指南.md"
            )
            self.assertTrue(main_source.is_file(), name)
            self.assertTrue(public_header.is_file(), name)
            self.assertTrue(guide.is_file(), name)
            self.assertFalse(any(directory.glob("*.md")), name)
            guide_text = guide.read_text(encoding="utf-8")
            for heading in (
                    "## 1. 组件定位", "## 2. DTS 配置", "## 3. API 参考",
                    "## 4. 使用流程", "## 5. 注意事项", "## 6. 验证"):
                self.assertIn(heading, guide_text, name)
            self.assertIn("| 属性 | 必填 | 类型/单位 | 说明 |", guide_text, name)
            self.assertIn("| API | 作用 |", guide_text, name)
            self.assertIn(
                "python -m studio.cli dts app/<app> --check", guide_text, name)
            self.assertIn(
                "python -m studio.cli dts app/<app>", guide_text, name)
            header_text = public_header.read_text(
                encoding="utf-8", errors="ignore")
            public_functions = re.findall(
                r"(?m)^\s*(?:bool|void|int32_t|int|size_t|"
                r"ark_component_result_t|const\s+[A-Za-z0-9_]+\s*\*)\s+"
                r"([a-z][a-z0-9_]+)\s*\(",
                header_text,
            )
            for function in public_functions:
                self.assertIn(f"{function}(", guide_text, f"{name}: {function}")
            self.assertIn(
                f"bool {name}_register(void)",
                main_source.read_text(encoding="utf-8", errors="ignore"),
                name,
            )
            for source in entry["sources"]:
                path = SDK_ROOT / "component" / source
                self.assertTrue(path.is_file(), source)
                self.assertNotIn(source, owners, source)
                owners[source] = name
                self.assertEqual(Path(source).parts[0], directory_name)

    def test_public_component_headers_do_not_expose_platform_types(self) -> None:
        catalog = json.loads(
            (SDK_ROOT / "component" / "common" / "component_catalog.json")
            .read_text(encoding="utf-8")
        )
        forbidden = (
            "ark_hal_", "FreeRTOS", "TaskHandle_t", "SemaphoreHandle_t",
            "QueueHandle_t", "UART_HandleTypeDef", "TIM_HandleTypeDef",
            "I2C_HandleTypeDef", "SPI_HandleTypeDef",
        )
        failures = []

        for entry in catalog["components"]:
            directory_name = entry.get("directory", entry["name"])
            header = (
                SDK_ROOT / "component" / directory_name /
                f"{entry['name']}.h"
            )
            text = header.read_text(encoding="utf-8", errors="ignore")
            if any(token in text for token in forbidden):
                failures.append(str(header.relative_to(SDK_ROOT)))

        self.assertEqual(failures, [])


if __name__ == "__main__":
    unittest.main()
