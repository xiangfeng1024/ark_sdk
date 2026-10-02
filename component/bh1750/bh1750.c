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

#include "bh1750.h"

#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "ark_cli.h"
#include "ark_component.h"
#include "ark_dts.h"
#include "ark_hal_i2c.h"

#define BH1750_ADDRESS             0x23U
#define BH1750_POWER_ON            0x01U
#define BH1750_CONTINUOUS_HIGH_RES 0x10U
#define BH1750_TIMEOUT_MS          100U

typedef struct {
    ark_hal_i2c_id_t i2c_id;
} bh1750_config_t;

static bh1750_config_t bh1750_config;

static bool bh1750_load_config(void) {
    const ark_of_node_t *node = ark_of_find_compatible_node(NULL, "bh1750");
    uint8_t i2c_id;

    if ((node == NULL) ||
        (ark_of_hal_get_parent(node, ARK_OF_HAL_I2C, &i2c_id) != 0)) {
        return false;
    }
    bh1750_config.i2c_id = (ark_hal_i2c_id_t)i2c_id;
    return true;
}

bool bh1750_read(bh1750_sample_t *sample) {
    uint8_t data[2];
    uint16_t raw;

    if ((sample == NULL) ||
        (ark_hal_i2c.receive(
             bh1750_config.i2c_id,
             BH1750_ADDRESS,
             data,
             sizeof(data),
             BH1750_TIMEOUT_MS) != (int32_t)sizeof(data))) {
        return false;
    }
    raw = (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
    sample->lux_x10 = ((uint32_t)raw * 100U) / 12U;
    sample->tick = xTaskGetTickCount();
    return true;
}

static ark_component_result_t bh1750_init(void) {
    uint8_t command;

    if (!bh1750_load_config() || !ark_hal_i2c.is_device_ready(
            bh1750_config.i2c_id, BH1750_ADDRESS, BH1750_TIMEOUT_MS)) {
        return ARK_COMPONENT_ERROR;
    }
    command = BH1750_POWER_ON;
    if (ark_hal_i2c.transmit(
            bh1750_config.i2c_id, BH1750_ADDRESS,
            &command, 1U, BH1750_TIMEOUT_MS) != 1) {
        return ARK_COMPONENT_ERROR;
    }
    command = BH1750_CONTINUOUS_HIGH_RES;
    if (ark_hal_i2c.transmit(
            bh1750_config.i2c_id, BH1750_ADDRESS,
            &command, 1U, BH1750_TIMEOUT_MS) != 1) {
        return ARK_COMPONENT_ERROR;
    }
    return ARK_COMPONENT_OK;
}

static ark_component_result_t bh1750_self_test(void) {
    bh1750_sample_t sample;

    vTaskDelay(pdMS_TO_TICKS(180U));
    return bh1750_read(&sample) ? ARK_COMPONENT_OK : ARK_COMPONENT_ERROR;
}

#if ARK_DTS_HAS_UART_CLI
static int bh1750_cli(int argc, char *argv[]) {
    bh1750_sample_t sample;

    if (argc != 1) {
        printf("usage: %s\r\n", argv[0]);
        return -1;
    }
    if (!bh1750_read(&sample)) {
        printf("bh1750 read failed\r\n");
        return -1;
    }
    printf("light=%lu.%lu lux\r\n",
           (unsigned long)(sample.lux_x10 / 10U),
           (unsigned long)(sample.lux_x10 % 10U));
    return 0;
}

static ark_cli_command_t bh1750_command = {
    .name = "light",
    .usage = "light",
    .description = "read BH1750 illuminance",
    .handler = bh1750_cli,
};
#endif

static const ark_component_t bh1750_component = {
    .name = "bh1750",
    .init = bh1750_init,
    .self_test = bh1750_self_test,
    .self_test_expected_ms = 300U,
    .init_level = ARK_COMPONENT_INIT_LEVEL_3,
};

bool bh1750_register(void) {
#if ARK_DTS_HAS_UART_CLI
    (void)ark_cli_register(&bh1750_command);
#endif
    return ark_component_register(&bh1750_component);
}
