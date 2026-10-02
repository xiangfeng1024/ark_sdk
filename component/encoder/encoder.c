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

#include "encoder.h"

#include "ark_dts_generated.h"

#if ARK_DTS_HAS_ENCODER

#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "ark_cli.h"
#include "ark_component.h"
#include "ark_dts.h"
#include "ark_hal_encoder.h"

#define ENCODER_RANGE 65536L
#define ENCODER_HALF 32768L

typedef struct {
    ark_hal_encoder_id_t hal_id;
    int8_t polarity;
} encoder_channel_config_t;

typedef struct {
    encoder_channel_config_t left;
    encoder_channel_config_t right;
    uint32_t counts_per_revolution;
} encoder_config_t;

typedef struct {
    uint16_t last_raw;
    int32_t count;
    int32_t last_report;
} encoder_state_t;

static encoder_state_t encoder_left_state;
static encoder_state_t encoder_right_state;
static encoder_config_t encoder_config;

static bool encoder_load_config(void) {
    const ark_of_node_t *node =
        ark_of_find_compatible_node(NULL, "encoder");
    uint8_t hal_id;
    int32_t polarity;

    if ((node == NULL) ||
        (ark_of_hal_get(node, "encoders", 0U, ARK_OF_HAL_ENCODER, &hal_id, NULL) != 0)) {
        return false;
    }
    encoder_config.left.hal_id = (ark_hal_encoder_id_t)hal_id;
    if (ark_of_hal_get(node, "encoders", 1U, ARK_OF_HAL_ENCODER, &hal_id, NULL) != 0) {
        return false;
    }
    encoder_config.right.hal_id = (ark_hal_encoder_id_t)hal_id;
    if (ark_of_property_read_s32(node, "ark,left-polarity", &polarity) != 0) {
        return false;
    }
    encoder_config.left.polarity = (int8_t)polarity;
    if (ark_of_property_read_s32(node, "ark,right-polarity", &polarity) != 0) {
        return false;
    }
    encoder_config.right.polarity = (int8_t)polarity;
    return ark_of_property_read_u32(
               node, "ark,counts-per-revolution",
               &encoder_config.counts_per_revolution) == 0;
}

static void encoder_update(
    encoder_state_t *state,
    const encoder_channel_config_t *config) {
    uint16_t now = ark_hal_encoder.get_count(config->hal_id);
    int32_t difference = (int32_t)now - (int32_t)state->last_raw;

    if (difference >= ENCODER_HALF) {
        difference -= ENCODER_RANGE;
    } else if (difference < -ENCODER_HALF) {
        difference += ENCODER_RANGE;
    }
    state->count += difference * config->polarity;
    state->last_raw = now;
}

void encoder_reset(void) {
    ark_hal_encoder.set_count(encoder_config.left.hal_id, 0U);
    ark_hal_encoder.set_count(encoder_config.right.hal_id, 0U);
    encoder_left_state = (encoder_state_t){0};
    encoder_right_state = (encoder_state_t){0};
}

bool encoder_get(encoder_data_t *data) {
    if (data == NULL) {
        return false;
    }
    taskENTER_CRITICAL();
    encoder_update(&encoder_left_state, &encoder_config.left);
    encoder_update(&encoder_right_state, &encoder_config.right);
    data->left_count = encoder_left_state.count;
    data->right_count = encoder_right_state.count;
    data->left_delta = encoder_left_state.count - encoder_left_state.last_report;
    data->right_delta = encoder_right_state.count - encoder_right_state.last_report;
    encoder_left_state.last_report = encoder_left_state.count;
    encoder_right_state.last_report = encoder_right_state.count;
    taskEXIT_CRITICAL();
    return true;
}

int32_t encoder_delta_to_rpm10(int32_t delta, uint32_t period_ms) {
    if ((period_ms == 0U) || (encoder_config.counts_per_revolution == 0U)) {
        return 0;
    }
    return (int32_t)(((int64_t)delta * 600000LL) /
        ((int64_t)encoder_config.counts_per_revolution * period_ms));
}

static ark_component_result_t encoder_init(void) {
    if (!encoder_load_config() ||
        !ark_hal_encoder.is_ready(encoder_config.left.hal_id) ||
        !ark_hal_encoder.is_ready(encoder_config.right.hal_id) ||
        !ark_hal_encoder.start(encoder_config.left.hal_id) ||
        !ark_hal_encoder.start(encoder_config.right.hal_id)) {
        return ARK_COMPONENT_ERROR;
    }
    encoder_reset();
    return ARK_COMPONENT_OK;
}

#if ARK_DTS_HAS_UART_CLI
static int encoder_cli(int argc, char *argv[]) {
    encoder_data_t data;
    uint16_t left_raw;
    uint16_t right_raw;

    if (argc != 1) {
        printf("usage: %s\r\n", argv[0]);
        return -1;
    }
    if (!encoder_get(&data)) {
        return -1;
    }
    left_raw = ark_hal_encoder.get_count(encoder_config.left.hal_id);
    right_raw = ark_hal_encoder.get_count(encoder_config.right.hal_id);
    printf("encoder left=%ld right=%ld delta=%ld/%ld raw=%u/%u\r\n",
           (long)data.left_count, (long)data.right_count,
           (long)data.left_delta, (long)data.right_delta,
           (unsigned int)left_raw, (unsigned int)right_raw);
    return 0;
}

static ark_cli_command_t encoder_command = {
    .name = "encoder",
    .usage = "encoder",
    .description = "show wheel encoder counts",
    .handler = encoder_cli,
};
#endif

static const ark_component_t encoder_component = {
    .name = "motor_encoder",
    .init = encoder_init,
    .self_test = NULL,
    .self_test_expected_ms = 0U,
    .init_level = ARK_COMPONENT_INIT_LEVEL_3,
};

bool encoder_register(void) {
#if ARK_DTS_HAS_UART_CLI
    (void)ark_cli_register(&encoder_command);
#endif
    return ark_component_register(&encoder_component);
}
#endif
