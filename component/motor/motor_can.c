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

#include "motor_can.h"

#include "ark_dts_generated.h"

#if ARK_DTS_HAS_MOTOR_CAN
#include <string.h>

#include "ark_component.h"
#include "ark_dts.h"
#include "ark_hal_can.h"
#include "motor_backend.h"

#define MOTOR_CAN_TIMEOUT_MS 100U
#define MOTOR_SELF_TEST_EXPECTED_MS 300U
#define MOTOR_TEST_RX_ID 0x1FFU

typedef enum {
    MOTOR_TRANSPORT_CAN = 0,
    MOTOR_TRANSPORT_PWM,
    MOTOR_TRANSPORT_UART
} motor_transport_t;

typedef struct {
    motor_transport_t transport;
    ark_hal_can_id_t can_id;
} motor_config_t;

static motor_config_t motor_config;

static bool motor_can_load_config(void) {
    const ark_of_node_t *node =
        ark_of_find_compatible_node(NULL, "motor_can");
    uint8_t can_id;

    if ((node == NULL) ||
        (ark_of_hal_get_parent(node, ARK_OF_HAL_CAN, &can_id) != 0)) {
        return false;
    }
    motor_config.transport = MOTOR_TRANSPORT_CAN;
    motor_config.can_id = (ark_hal_can_id_t)can_id;
    return true;
}

static bool motor_can_set_loopback(bool enabled) {
    return (motor_config.transport == MOTOR_TRANSPORT_CAN) &&
           (ark_hal_can.set_loopback(motor_config.can_id, enabled) == 0) &&
           (ark_hal_can.start(motor_config.can_id) == 0);
}

static bool motor_can_transmit(uint32_t id, const uint8_t *data, uint8_t size) {
    ark_hal_can_frame_t frame;

    if ((data == NULL) || (size > 8U)) {
        return false;
    }
    memset(&frame, 0, sizeof(frame));
    frame.id = id;
    frame.length = size;
    memcpy(frame.data, data, size);
    return ark_hal_can.transmit(
               motor_config.can_id, &frame, MOTOR_CAN_TIMEOUT_MS) == (int32_t)size;
}

static bool motor_can_receive(ark_hal_can_frame_t *frame, uint32_t timeout_ms) {
    return (frame != NULL) &&
           (ark_hal_can.receive(motor_config.can_id, frame, timeout_ms) >= 0);
}

static bool motor_can_set_currents(
    int16_t m1,
    int16_t m2,
    int16_t m3,
    int16_t m4) {
    uint8_t data[8] = {
        (uint8_t)(m1 >> 8U), (uint8_t)m1,
        (uint8_t)(m2 >> 8U), (uint8_t)m2,
        (uint8_t)(m3 >> 8U), (uint8_t)m3,
        (uint8_t)(m4 >> 8U), (uint8_t)m4,
    };

    return motor_can_transmit(0x200U, data, sizeof(data));
}

static bool motor_can_is_ready(void) {
    return (motor_config.transport == MOTOR_TRANSPORT_CAN) &&
           ark_hal_can.is_ready(motor_config.can_id);
}

static void motor_can_stop(void) {
    (void)motor_can_set_currents(0, 0, 0, 0);
}

static const motor_backend_t motor_can_backend = {
    .name = "can_m3508",
    .is_ready = motor_can_is_ready,
    .set_duty_permille = NULL,
    .set_currents = motor_can_set_currents,
    .stop = motor_can_stop,
};

static ark_component_result_t motor_can_init(void) {
    if (!motor_can_load_config()) {
        return ARK_COMPONENT_ERROR;
    }
    if ((motor_config.transport != MOTOR_TRANSPORT_CAN) ||
        !ark_hal_can.is_ready(motor_config.can_id)) {
        return ARK_COMPONENT_ERROR;
    }
    if ((ark_hal_can.configure_std_filter(
             motor_config.can_id, MOTOR_TEST_RX_ID) != 0) ||
        (ark_hal_can.start(motor_config.can_id) != 0)) {
        return ARK_COMPONENT_ERROR;
    }
    return motor_backend_attach(&motor_can_backend) ?
           ARK_COMPONENT_OK : ARK_COMPONENT_ERROR;
}

static ark_component_result_t motor_can_self_test(void) {
    static const uint8_t demo[8] = {0x10U, 0x20U, 0x30U, 0x40U, 0x50U, 0x60U, 0x70U, 0x80U};
    ark_hal_can_frame_t received;

    if (!motor_can_set_loopback(true) ||
        !motor_can_transmit(MOTOR_TEST_RX_ID, demo, sizeof(demo)) ||
        !motor_can_receive(&received, MOTOR_CAN_TIMEOUT_MS) ||
        (received.id != MOTOR_TEST_RX_ID) || (received.length != sizeof(demo)) ||
        (memcmp(received.data, demo, sizeof(demo)) != 0)) {
        return ARK_COMPONENT_ERROR;
    }
    return motor_can_set_loopback(false) ? ARK_COMPONENT_OK : ARK_COMPONENT_ERROR;
}

static ark_component_t motor_can_component = {
    .name = "motor_can",
    .init = motor_can_init,
    .self_test = motor_can_self_test,
    .self_test_expected_ms = MOTOR_SELF_TEST_EXPECTED_MS,
    .init_level = ARK_COMPONENT_INIT_LEVEL_2,
};

bool motor_can_register(void) {
    return ark_component_register(&motor_can_component);
}
#else
bool motor_can_register(void) {
    return false;
}
#endif
