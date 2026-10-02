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

#include "dht11.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "ark_cli.h"
#include "ark_component.h"
#include "ark_dts.h"
#include "ark_hal_gpio.h"
#include "ark_hal_time.h"

#define DHT11_START_LOW_MS          20U
#define DHT11_START_RELEASE_US      30U
#define DHT11_EDGE_TIMEOUT_US       120U
#define DHT11_DATA_BIT_THRESHOLD_US 50U

typedef struct {
    ark_hal_gpio_config_t DATA;
} dht11_config_t;

static dht11_config_t dht11_config;

static bool dht11_load_config(void) {
    const ark_of_node_t *node = ark_of_find_compatible_node(NULL, "dht11");

    return (node != NULL) &&
        (ark_of_get_named_gpio(node, "data-gpios", 0U, &dht11_config.DATA) == 0);
}

static bool dht11_wait_level(bool expected_level, uint32_t timeout_us) {
    uint32_t frequency = ark_hal_time.frequency_hz();
    uint32_t started = ark_hal_time.cycles();
    uint32_t timeout_cycles = (frequency / 1000000U) * timeout_us;

    while (ark_hal_gpio.get(
               dht11_config.DATA.port,
               dht11_config.DATA.pin) != expected_level) {
        if ((uint32_t)(ark_hal_time.cycles() - started) >= timeout_cycles) {
            return false;
        }
    }
    return true;
}

bool dht11_read(dht11_sample_t *sample) {
    uint8_t data[5] = {0};
    size_t bit;
    bool is_valid = false;

    if ((sample == NULL) || !ark_hal_time.is_ready()) {
        return false;
    }
    ark_hal_gpio.configure(
        dht11_config.DATA.port,
        dht11_config.DATA.pin,
        ARK_HAL_GPIO_MODE_OUTPUT_OPEN_DRAIN,
        true);
    ark_hal_gpio.set(dht11_config.DATA.port, dht11_config.DATA.pin, false);
    vTaskDelay(pdMS_TO_TICKS(DHT11_START_LOW_MS));
    ark_hal_gpio.set(dht11_config.DATA.port, dht11_config.DATA.pin, true);
    ark_hal_time.delay_us(DHT11_START_RELEASE_US);
    ark_hal_gpio.configure(
        dht11_config.DATA.port,
        dht11_config.DATA.pin,
        ARK_HAL_GPIO_MODE_INPUT,
        true);

    taskENTER_CRITICAL();
    if (dht11_wait_level(false, DHT11_EDGE_TIMEOUT_US) &&
        dht11_wait_level(true, DHT11_EDGE_TIMEOUT_US) &&
        dht11_wait_level(false, DHT11_EDGE_TIMEOUT_US)) {
        is_valid = true;
        for (bit = 0U; bit < 40U; ++bit) {
            uint32_t started;
            uint32_t high_cycles;

            if (!dht11_wait_level(true, DHT11_EDGE_TIMEOUT_US)) {
                is_valid = false;
                break;
            }
            started = ark_hal_time.cycles();
            if (!dht11_wait_level(false, DHT11_EDGE_TIMEOUT_US)) {
                is_valid = false;
                break;
            }
            high_cycles = ark_hal_time.cycles() - started;
            data[bit / 8U] <<= 1U;
            if (high_cycles >
                (ark_hal_time.frequency_hz() / 1000000U) *
                    DHT11_DATA_BIT_THRESHOLD_US) {
                data[bit / 8U] |= 1U;
            }
        }
    }
    taskEXIT_CRITICAL();
    if (!is_valid ||
        ((uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4])) {
        return false;
    }
    sample->humidity_deci_percent = (uint16_t)data[0] * 10U + data[1];
    sample->temperature_deci_c = (int16_t)data[2] * 10 + data[3];
    sample->tick = xTaskGetTickCount();
    return true;
}

static ark_component_result_t dht11_init(void) {
    if (!dht11_load_config() || !ark_hal_time.is_ready()) {
        return ARK_COMPONENT_ERROR;
    }
    ark_hal_gpio.set(dht11_config.DATA.port, dht11_config.DATA.pin, true);
    return ARK_COMPONENT_OK;
}

static ark_component_result_t dht11_self_test(void) {
    dht11_sample_t sample;

    return dht11_read(&sample) ? ARK_COMPONENT_OK : ARK_COMPONENT_ERROR;
}

#if ARK_DTS_HAS_UART_CLI
static int dht11_cli(int argc, char *argv[]) {
    dht11_sample_t sample;

    if (argc != 1) {
        printf("usage: %s\r\n", argv[0]);
        return -1;
    }
    if (!dht11_read(&sample)) {
        printf("dht11 read failed\r\n");
        return -1;
    }
    printf(
        "temperature=%d.%d C humidity=%u.%u %%\r\n",
        sample.temperature_deci_c / 10,
        sample.temperature_deci_c < 0 ?
            -(sample.temperature_deci_c % 10) : sample.temperature_deci_c % 10,
        sample.humidity_deci_percent / 10U,
        sample.humidity_deci_percent % 10U);
    return 0;
}

static ark_cli_command_t dht11_command = {
    .name = "dht",
    .usage = "dht",
    .description = "read DHT11 temperature and humidity",
    .handler = dht11_cli,
};
#endif

static const ark_component_t dht11_component = {
    .name = "dht11",
    .init = dht11_init,
    .self_test = dht11_self_test,
    .self_test_expected_ms = 100U,
    .init_level = ARK_COMPONENT_INIT_LEVEL_3,
};

bool dht11_register(void) {
#if ARK_DTS_HAS_UART_CLI
    (void)ark_cli_register(&dht11_command);
#endif
    return ark_component_register(&dht11_component);
}
