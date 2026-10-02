/**
 * SPDX-FileCopyrightText: Copyright (c) 2026 Ark Crew. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-Ark-Crew-NonCommercial-1.0
 *
 * ARK CREW LIMITED NON-COMMERCIAL LICENSE NOTICE
 *
 * This source code, together with its associated documentation, examples,
 * configuration files, and related materials, is collectively referred to
 * as the "Software".
 *
 * Subject to the complete terms set forth in the LICENSE file, Ark Crew
 * grants you a limited, non-exclusive, non-transferable, and non-sublicensable
 * right to access, reproduce, and modify the Software solely for personal
 * study, classroom education, academic research, and non-commercial evaluation.
 *
 * Commercial use of the Software, in whole or in part, is strictly prohibited
 * without prior written authorization from Ark Crew. Prohibited activities
 * include, without limitation, sale, sublicensing, paid distribution, use in
 * paid consulting or training, incorporation into any commercial product or
 * service, and internal development intended for commercial deployment.
 *
 * Except for the limited rights expressly granted under the applicable
 * License, no license or other right, whether express, implied, by estoppel,
 * or otherwise, is granted under any copyright, patent, trademark, trade
 * secret, mask work, or other intellectual property right belonging to
 * Ark Crew or any third party.
 *
 * Delivery or disclosure of the Software does not convey permission to use
 * the Ark Crew name, trademarks, logos, visual identity, or other branding,
 * except where strictly necessary to preserve the original attribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND "WITH ALL FAULTS", WITHOUT ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND, WHETHER EXPRESS, IMPLIED,
 * STATUTORY, OR OTHERWISE, INCLUDING WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, TITLE, ACCURACY, RELIABILITY, AND
 * NON-INFRINGEMENT.
 *
 * TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, ARK CREW SHALL NOT
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * PUNITIVE, OR CONSEQUENTIAL LOSS OR DAMAGE ARISING FROM OR RELATED TO
 * THE SOFTWARE, ITS USE, OR ITS INABILITY TO BE USED.
 *
 * This notice shall be retained in all authorized copies or substantial
 * portions of the Software. Removal, concealment, or unauthorized alteration
 * of this notice is prohibited.
 *
 * See the LICENSE file in the root directory of this repository for the
 * complete and controlling license terms.
 */

#include "led.h"

#include <stdio.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "ark_dts_generated.h"
#include "ark_dts.h"
#include "ark_cli.h"
#include "ark_component.h"
#include "ark_hal_gpio.h"

#define LED_SELF_TEST_FLASHES 3U
#define LED_SELF_TEST_ON_MS 90U
#define LED_SELF_TEST_OFF_MS 90U
#define LED_SELF_TEST_EXPECTED_MS 600U
#define LED_MAX_INSTANCES 16U

typedef struct {
    size_t id;
    ark_hal_gpio_config_t gpio;
} led_instance_config_t;

typedef struct {
    const led_instance_config_t *instances;
    size_t count;
} led_config_t;

static led_instance_config_t led_instances[LED_MAX_INSTANCES];
static led_config_t led_config;

static bool led_load_config(void) {
    const ark_of_node_t *parent =
        ark_of_find_compatible_node(NULL, "led");
    const ark_of_node_t *child = NULL;
    size_t count = 0U;

    if (parent == NULL) {
        return false;
    }
    while ((child = ark_of_get_next_available_child(parent, child)) != NULL) {
        uint32_t id = (uint32_t)count;

        if ((count >= LED_MAX_INSTANCES) ||
            (ark_of_get_named_gpio(child, "gpios", 0U,
                                  &led_instances[count].gpio) != 0)) {
            return false;
        }
        ark_of_property_read_u32(child, "reg", &id);
        led_instances[count].id = (size_t)id;
        ++count;
    }
    led_config.instances = led_instances;
    led_config.count = count;
    return count > 0U;
}

static const led_instance_config_t *led_find(size_t id) {
    size_t index;

    for (index = 0U; index < led_config.count; ++index) {
        if (led_config.instances[index].id == id) {
            return &led_config.instances[index];
        }
    }
    return NULL;
}

static void led_set(const led_instance_config_t *led, bool on) {
    ark_hal_gpio.set(
        led->gpio.port,
        led->gpio.pin,
        on ? (led->gpio.active_level != 0U) : (led->gpio.active_level == 0U));
}

static bool led_read(const led_instance_config_t *led) {
    bool level = ark_hal_gpio.get(led->gpio.port, led->gpio.pin);

    return level == (led->gpio.active_level != 0U);
}

ark_component_result_t led_init(void) {
    size_t index;

    for (index = 0U; index < led_count(); ++index) {
        led_set(&led_config.instances[index], false);
    }

    return ARK_COMPONENT_OK;
}

ark_component_result_t led_self_test(void) {
    size_t index;
    size_t flash;

    for (index = 0U; index < led_config.count; ++index) {
        for (flash = 0U; flash < LED_SELF_TEST_FLASHES; ++flash) {
            led_set(&led_config.instances[index], true);
            vTaskDelay(pdMS_TO_TICKS(LED_SELF_TEST_ON_MS));
            if (!led_read(&led_config.instances[index])) {
                led_set(&led_config.instances[index], false);
                return ARK_COMPONENT_ERROR;
            }

            led_set(&led_config.instances[index], false);
            vTaskDelay(pdMS_TO_TICKS(LED_SELF_TEST_OFF_MS));
            if (led_read(&led_config.instances[index])) {
                return ARK_COMPONENT_ERROR;
            }
        }
    }

    return ARK_COMPONENT_OK;
}

static ark_component_t led_component = {
    .name = "led",
    .init = NULL,
    .self_test = NULL,
    .self_test_expected_ms = 0U,
    .init_level = ARK_COMPONENT_INIT_LEVEL_3,
};

#if ARK_DTS_HAS_UART_CLI
static int led_cli(int argc, char *argv[]) {
    char *id_end;
    char *value_end;
    unsigned long id;
    unsigned long value;

    if (argc != 3) {
        printf("usage: %s <id> <0|1>\r\n", argv[0]);
        return -1;
    }
    id = strtoul(argv[1], &id_end, 10);
    value = strtoul(argv[2], &value_end, 10);
    if ((*id_end != '\0') || (*value_end != '\0') || (value > 1U) ||
        !led_set_by_id((size_t)id, value == 1U)) {
        printf("invalid led id or value\r\n");
        return -1;
    }
    printf("led %lu = %lu\r\n", id, value);
    return 0;
}

static ark_cli_command_t led_command = {
    .name = "led",
    .usage = "led <id> <0|1>",
    .description = "set an LED instance",
    .handler = led_cli,
};
#endif

bool led_register(void) {
    if (!led_load_config()) {
        return false;
    }
#if ARK_DTS_HAS_UART_CLI
    (void)ark_cli_register(&led_command);
#endif
    return ark_component_register(&led_component);
}

bool led_set_by_id(size_t id, bool on) {
    const led_instance_config_t *led = led_find(id);

    if (led == NULL) {
        return false;
    }

    led_set(led, on);
    return true;
}

bool led_toggle_by_id(size_t id) {
    const led_instance_config_t *led = led_find(id);

    if (led == NULL) {
        return false;
    }

    ark_hal_gpio.toggle(led->gpio.port, led->gpio.pin);
    return true;
}

size_t led_count(void) {
    return led_config.count;
}
