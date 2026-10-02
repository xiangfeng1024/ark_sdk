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

#include "motor_tb6612.h"

#include "ark_dts_generated.h"

#if ARK_DTS_HAS_MOTOR_TB6612

#include <stdio.h>
#include <stdlib.h>

#include "ark_cli.h"
#include "ark_component.h"
#include "ark_dts.h"
#include "ark_hal_gpio.h"
#include "ark_hal_pwm.h"
#include "motor_backend.h"

typedef struct {
    ark_hal_pwm_id_t pwm_id;
    ark_hal_gpio_config_t IN1;
    ark_hal_gpio_config_t IN2;
    int8_t polarity;
} tb6612_channel_config_t;

typedef struct {
    tb6612_channel_config_t left;
    tb6612_channel_config_t right;
} tb6612_config_t;

static tb6612_config_t tb6612_config;

static bool motor_tb6612_load_config(void) {
    const ark_of_node_t *node =
        ark_of_find_compatible_node(NULL, "motor_tb6612");
    uint8_t pwm_id;
    int32_t polarity;

    if ((node == NULL) ||
        (ark_of_hal_get(node, "pwms", 0U, ARK_OF_HAL_PWM, &pwm_id, NULL) != 0)) {
        return false;
    }
    tb6612_config.left.pwm_id = (ark_hal_pwm_id_t)pwm_id;
    if (ark_of_hal_get(node, "pwms", 1U, ARK_OF_HAL_PWM, &pwm_id, NULL) != 0) {
        return false;
    }
    tb6612_config.right.pwm_id = (ark_hal_pwm_id_t)pwm_id;
    if ((ark_of_get_named_gpio(node, "left-in1-gpios", 0U, &tb6612_config.left.IN1) != 0) ||
        (ark_of_get_named_gpio(node, "left-in2-gpios", 0U, &tb6612_config.left.IN2) != 0) ||
        (ark_of_get_named_gpio(node, "right-in1-gpios", 0U, &tb6612_config.right.IN1) != 0) ||
        (ark_of_get_named_gpio(node, "right-in2-gpios", 0U, &tb6612_config.right.IN2) != 0) ||
        (ark_of_property_read_s32(node, "ark,left-polarity", &polarity) != 0)) {
        return false;
    }
    tb6612_config.left.polarity = (int8_t)polarity;
    if (ark_of_property_read_s32(node, "ark,right-polarity", &polarity) != 0) {
        return false;
    }
    tb6612_config.right.polarity = (int8_t)polarity;
    return true;
}

static const tb6612_channel_config_t *motor_tb6612_channel(uint8_t channel) {
    return (channel == 0U) ? &tb6612_config.left :
           (channel == 1U) ? &tb6612_config.right : NULL;
}

#if ARK_DTS_HAS_UART_CLI
static bool tb6612_initialized;
#endif

static bool motor_tb6612_set_duty(
    uint8_t channel_id,
    int16_t duty_permille) {
    const tb6612_channel_config_t *channel =
        motor_tb6612_channel(channel_id);
    int32_t command = duty_permille;
    uint32_t period;
    uint32_t compare;

    if ((channel == NULL) || (duty_permille < -1000) || (duty_permille > 1000)) {
        return false;
    }
    command *= channel->polarity;
    if (command == 0) {
        ark_hal_gpio.set(channel->IN1.port, channel->IN1.pin, false);
        ark_hal_gpio.set(channel->IN2.port, channel->IN2.pin, false);
    } else {
        ark_hal_gpio.set(channel->IN1.port, channel->IN1.pin, command > 0);
        ark_hal_gpio.set(channel->IN2.port, channel->IN2.pin, command < 0);
    }
    period = ark_hal_pwm.period_ticks(channel->pwm_id);
    compare = ((uint32_t)((command < 0) ? -command : command) * period) / 1000U;
    return ark_hal_pwm.set_compare(channel->pwm_id, compare);
}

static void motor_tb6612_stop(void) {
    (void)motor_tb6612_set_duty(0U, 0);
    (void)motor_tb6612_set_duty(1U, 0);
}

static bool motor_tb6612_is_ready(void) {
#if ARK_DTS_HAS_UART_CLI
    return tb6612_initialized;
#else
    return ark_hal_pwm.is_ready(tb6612_config.left.pwm_id) &&
           ark_hal_pwm.is_ready(tb6612_config.right.pwm_id);
#endif
}

static const motor_backend_t motor_tb6612_backend = {
    .name = "tb6612",
    .is_ready = motor_tb6612_is_ready,
    .set_duty_permille = motor_tb6612_set_duty,
    .set_currents = NULL,
    .stop = motor_tb6612_stop,
};

static ark_component_result_t motor_tb6612_init(void) {
    if (!motor_tb6612_load_config()) {
        return ARK_COMPONENT_ERROR;
    }
    bool left_ready = ark_hal_pwm.is_ready(tb6612_config.left.pwm_id);
    bool right_ready = ark_hal_pwm.is_ready(tb6612_config.right.pwm_id);
    bool left_started = left_ready && ark_hal_pwm.start(tb6612_config.left.pwm_id);
    bool right_started = right_ready && ark_hal_pwm.start(tb6612_config.right.pwm_id);

    if (!left_started || !right_started) {
        return ARK_COMPONENT_ERROR;
    }
#if ARK_DTS_HAS_UART_CLI
    tb6612_initialized = true;
#endif
    motor_tb6612_stop();
    return motor_backend_attach(&motor_tb6612_backend) ?
           ARK_COMPONENT_OK : ARK_COMPONENT_ERROR;
}

#if ARK_DTS_HAS_UART_CLI
static int tb6612_cli(int argc, char *argv[]) {
    char *end;
    unsigned long id;
    long duty;

    if (argc != 3) {
        printf("usage: %s <0|1> <-1000..1000>\r\n", argv[0]);
        return -1;
    }
    id = strtoul(argv[1], &end, 10);
    if ((*end != '\0') || (id > 1U)) {
        return -1;
    }
    duty = strtol(argv[2], &end, 10);
    if ((*end != '\0') || (duty < -1000L) || (duty > 1000L) ||
        !motor_tb6612_set_duty((uint8_t)id, (int16_t)duty)) {
        printf("motor %lu failed initialized=%u period=%lu\r\n",
               id,
               tb6612_initialized ? 1U : 0U,
               (unsigned long)ark_hal_pwm.period_ticks(
                   (id == 0U) ? tb6612_config.left.pwm_id :
                                tb6612_config.right.pwm_id));
        return -1;
    }
    printf("motor %lu duty=%ld\r\n", id, duty);
    return 0;
}

static ark_cli_command_t tb6612_command = {
    .name = "motor",
    .usage = "motor <0|1> <-1000..1000>",
    .description = "set TB6612 motor duty",
    .handler = tb6612_cli,
};
#endif

static const ark_component_t motor_tb6612_component = {
    .name = "motor_tb6612",
    .init = motor_tb6612_init,
    .self_test = NULL,
    .self_test_expected_ms = 0U,
    .init_level = ARK_COMPONENT_INIT_LEVEL_2,
};

bool motor_tb6612_register(void) {
#if ARK_DTS_HAS_UART_CLI
    (void)ark_cli_register(&tb6612_command);
#endif
    return ark_component_register(&motor_tb6612_component);
}
#endif
