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

#include "servo.h"

#include "ark_dts_generated.h"

#if ARK_DTS_HAS_SERVO

#include <stdio.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "ark_cli.h"
#include "ark_component.h"
#include "ark_dts.h"
#include "ark_hal_gpio.h"
#include "ark_hal_pwm.h"
#include "ark_hal_time.h"

#define SERVO_MIN_PULSE_US 500U
#define SERVO_MAX_PULSE_US 2500U
#define SERVO_FRAME_PERIOD_US 20000U
#define SERVO_RAW_COMMAND_DURATION_MS 200U
#define SERVO_RESET_DURATION_MS 1000U
#define SERVO_MEASURED_MOVE_DEG 45U
#define SERVO_MEASURED_MOVE_MS 200U

typedef enum {
    SERVO_TRANSPORT_HARDWARE_PWM = 0,
    SERVO_TRANSPORT_SOFTWARE_PWM
} servo_transport_t;

typedef struct {
    servo_transport_t transport;
    ark_hal_pwm_id_t pwm_id;
    ark_hal_gpio_config_t DATA;
} servo_config_t;

static int16_t servo_last_angle = SERVO_RESET_ANGLE;
static servo_config_t servo_config;

static bool servo_load_config(void) {
    const ark_of_node_t *node =
        ark_of_find_compatible_node(NULL, "servo");

    if ((node != NULL) &&
        ark_of_property_read_bool(node, "ark,software-pwm")) {
        servo_config.transport = SERVO_TRANSPORT_SOFTWARE_PWM;
        return ark_of_get_named_gpio(node, "data-gpios", 0U, &servo_config.DATA) == 0;
    }
    if (node != NULL) {
        uint8_t pwm_id;
        if (ark_of_hal_get(node, "pwms", 0U, ARK_OF_HAL_PWM, &pwm_id, NULL) != 0) {
            return false;
        }
        servo_config.transport = SERVO_TRANSPORT_HARDWARE_PWM;
        servo_config.pwm_id = (ark_hal_pwm_id_t)pwm_id;
        return true;
    }
    return false;
}

static bool servo_software_burst(uint16_t pulse_us, uint32_t duration_ms) {
    uint32_t frame;
    uint32_t frame_count =
        (duration_ms * 1000U + SERVO_FRAME_PERIOD_US - 1U) /
        SERVO_FRAME_PERIOD_US;
    TickType_t next_frame;
    bool high_level = servo_config.DATA.active_level != 0U;
    bool low_level = !high_level;

    if (frame_count == 0U) {
        return true;
    }
    next_frame = xTaskGetTickCount();
    for (frame = 0U; frame < frame_count; ++frame) {
        vTaskSuspendAll();
        ark_hal_gpio.set(
            servo_config.DATA.port,
            servo_config.DATA.pin,
            high_level);
        ark_hal_time.delay_us(pulse_us);
        ark_hal_gpio.set(
            servo_config.DATA.port,
            servo_config.DATA.pin,
            low_level);
        (void)xTaskResumeAll();
        vTaskDelayUntil(
            &next_frame,
            pdMS_TO_TICKS(SERVO_FRAME_PERIOD_US / 1000U));
    }
    return true;
}

static uint32_t servo_move_duration_ms(int16_t from_angle, int16_t to_angle) {
    uint32_t delta = (uint32_t)abs((int)to_angle - (int)from_angle);

    return (delta * SERVO_MEASURED_MOVE_MS + SERVO_MEASURED_MOVE_DEG - 1U) /
           SERVO_MEASURED_MOVE_DEG;
}

bool servo_set_pulse_us(uint16_t pulse_us) {
    if ((pulse_us < SERVO_MIN_PULSE_US) || (pulse_us > SERVO_MAX_PULSE_US)) {
        return false;
    }
    if (servo_config.transport == SERVO_TRANSPORT_SOFTWARE_PWM) {
        return servo_software_burst(pulse_us, SERVO_RAW_COMMAND_DURATION_MS);
    }
    return ark_hal_pwm.set_compare(servo_config.pwm_id, pulse_us);
}

bool servo_set_angle(int16_t angle) {
    int32_t pulse;

    if ((angle < SERVO_MIN_ANGLE) || (angle > SERVO_MAX_ANGLE)) {
        return false;
    }
    pulse = SERVO_MIN_PULSE_US +
        ((int32_t)angle * (SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US)) /
        SERVO_MAX_ANGLE;
    if (servo_config.transport == SERVO_TRANSPORT_SOFTWARE_PWM) {
        uint32_t duration_ms = servo_move_duration_ms(servo_last_angle, angle);

        if (!servo_software_burst((uint16_t)pulse, duration_ms)) {
            return false;
        }
    } else if (!servo_set_pulse_us((uint16_t)pulse)) {
        return false;
    }
    servo_last_angle = angle;
    return true;
}

bool servo_reset(void) {
    if (servo_config.transport == SERVO_TRANSPORT_SOFTWARE_PWM) {
        if (!servo_software_burst(SERVO_MIN_PULSE_US, SERVO_RESET_DURATION_MS)) {
            return false;
        }
        servo_last_angle = SERVO_RESET_ANGLE;
        return true;
    }
    return servo_set_angle(SERVO_RESET_ANGLE);
}

void servo_disable(void) {
    if (servo_config.transport == SERVO_TRANSPORT_SOFTWARE_PWM) {
        ark_hal_gpio.set(
            servo_config.DATA.port,
            servo_config.DATA.pin,
            servo_config.DATA.active_level == 0U);
    } else {
        ark_hal_pwm.stop(servo_config.pwm_id);
    }
}

static ark_component_result_t servo_init(void) {
    if (!servo_load_config()) {
        return ARK_COMPONENT_ERROR;
    }
    if (servo_config.transport == SERVO_TRANSPORT_SOFTWARE_PWM) {
        if (!ark_hal_time.is_ready() ||
            !ark_hal_gpio.configure(
                servo_config.DATA.port,
                servo_config.DATA.pin,
                ARK_HAL_GPIO_MODE_OUTPUT_PUSH_PULL,
                false)) {
            return ARK_COMPONENT_ERROR;
        }
        if (!servo_reset()) {
            return ARK_COMPONENT_ERROR;
        }
    } else if (!ark_hal_pwm.is_ready(servo_config.pwm_id) ||
               !ark_hal_pwm.set_compare(servo_config.pwm_id, 0U) ||
               !ark_hal_pwm.start(servo_config.pwm_id) ||
               !servo_reset()) {
        return ARK_COMPONENT_ERROR;
    }
    return ARK_COMPONENT_OK;
}

#if ARK_DTS_HAS_UART_CLI
static int servo_cli(int argc, char *argv[]) {
    char *end;
    long angle;

    if (argc != 2) {
        printf("usage: %s <0..180>\r\n", argv[0]);
        return -1;
    }
    angle = strtol(argv[1], &end, 10);
    if ((*end != '\0') || (angle < SERVO_MIN_ANGLE) ||
        (angle > SERVO_MAX_ANGLE) || !servo_set_angle((int16_t)angle)) {
        return -1;
    }
    printf("servo angle=%ld\r\n", angle);
    return 0;
}

static ark_cli_command_t servo_command = {
    .name = "servo",
    .usage = "servo <0..180>",
    .description = "set servo angle",
    .handler = servo_cli,
};
#endif

static const ark_component_t servo_component = {
    .name = "motor_servo",
    .init = servo_init,
    .self_test = NULL,
    .self_test_expected_ms = 0U,
    .init_level = ARK_COMPONENT_INIT_LEVEL_3,
};

bool servo_register(void) {
#if ARK_DTS_HAS_UART_CLI
    (void)ark_cli_register(&servo_command);
#endif
    return ark_component_register(&servo_component);
}
#endif
