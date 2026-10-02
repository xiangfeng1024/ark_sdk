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

#include "bluetooth.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "ark_component.h"
#include "ark_dts.h"
#include "ark_stream.h"
#include "ark_uart_codec.h"

#define BLUETOOTH_RING_SIZE 128U
#define BLUETOOTH_TASK_STACK_DEPTH 128U
#define BLUETOOTH_PACKET_HEADER 0xA5U
#define BLUETOOTH_PACKET_TAIL 0x5AU
#define BLUETOOTH_PACKET_MAX_SIZE \
    (3U + BLUETOOTH_PACKET_UINT8_CAPACITY + \
     BLUETOOTH_PACKET_SHORT_CAPACITY * 2U + \
     BLUETOOTH_PACKET_INT_CAPACITY * 4U + \
     BLUETOOTH_PACKET_FLOAT_CAPACITY * 4U)
#define BLUETOOTH_PACKET_SIZE (BLUETOOTH_CONTROL_DATA_SIZE + 3U)

typedef struct {
    uint8_t data[BLUETOOTH_RING_SIZE];
    uint16_t read;
    uint16_t write;
} bluetooth_ring_t;

typedef struct {
    ark_stream_t *stream;
    bluetooth_ring_t ring;
    bluetooth_control_t control;
    uint8_t packet[BLUETOOTH_PACKET_SIZE];
    uint8_t packet_index;
    uint8_t tx_packet[BLUETOOTH_PACKET_MAX_SIZE];
    TaskHandle_t task;
    bool ready;
} bluetooth_context_t;

static bluetooth_context_t bluetooth_context;

static bool bluetooth_load_config(void) {
    const ark_of_node_t *node = ark_of_find_compatible_node(NULL, "bluetooth");
    const ark_of_node_t *parent;
    const char *stream_name;

    if (node == NULL) {
        return false;
    }
    parent = ark_of_get_parent(node);
    if ((parent == NULL) ||
        (ark_of_property_read_string(parent, "ark,stream-name", &stream_name) != 0)) {
        return false;
    }
    bluetooth_context.stream = ark_stream_find(stream_name);
    return bluetooth_context.stream != NULL;
}

static void bluetooth_ring_put(uint8_t byte) {
    uint16_t next = (uint16_t)((bluetooth_context.ring.write + 1U) %
                               BLUETOOTH_RING_SIZE);

    if (next == bluetooth_context.ring.read) {
        bluetooth_context.ring.read = (uint16_t)(
            (bluetooth_context.ring.read + 1U) % BLUETOOTH_RING_SIZE);
    }
    bluetooth_context.ring.data[bluetooth_context.ring.write] = byte;
    bluetooth_context.ring.write = next;
}

static int32_t bluetooth_ring_read(uint8_t *data, size_t size) {
    size_t count = 0U;

    if ((data == NULL) || (size == 0U)) {
        return -1;
    }
    taskENTER_CRITICAL();
    while ((count < size) &&
           (bluetooth_context.ring.read != bluetooth_context.ring.write)) {
        data[count++] = bluetooth_context.ring.data[bluetooth_context.ring.read];
        bluetooth_context.ring.read = (uint16_t)(
            (bluetooth_context.ring.read + 1U) % BLUETOOTH_RING_SIZE);
    }
    taskEXIT_CRITICAL();
    return (int32_t)count;
}

static bool bluetooth_parse_byte(uint8_t byte) {
    bluetooth_control_t next;

    if (bluetooth_context.packet_index == 0U) {
        if (byte != BLUETOOTH_PACKET_HEADER) {
            return false;
        }
    }
    bluetooth_context.packet[bluetooth_context.packet_index++] = byte;
    if (bluetooth_context.packet_index < BLUETOOTH_PACKET_SIZE) {
        return false;
    }
    bluetooth_context.packet_index = 0U;
    if ((bluetooth_context.packet[BLUETOOTH_PACKET_SIZE - 1U] !=
         BLUETOOTH_PACKET_TAIL) ||
        (bluetooth_context.packet[BLUETOOTH_PACKET_SIZE - 2U] !=
         ark_uart_checksum_sum(&bluetooth_context.packet[1],
                              BLUETOOTH_CONTROL_DATA_SIZE))) {
        return false;
    }
    memset(&next, 0, sizeof(next));
    memcpy(next.data, &bluetooth_context.packet[1], sizeof(next.data));
    taskENTER_CRITICAL();
    next.sequence = bluetooth_context.control.sequence + 1U;
    next.tick = xTaskGetTickCount();
    bluetooth_context.control = next;
    taskEXIT_CRITICAL();
    return true;
}

static void bluetooth_task(void *argument) {
    uint8_t data[32];

    (void)argument;
    for (;;) {
        int32_t size = ark_stream_read(
            bluetooth_context.stream, data, sizeof(data), ARK_STREAM_WAIT_FOREVER);
        int32_t index;

        if (size <= 0) {
            continue;
        }
        for (index = 0; index < size; ++index) {
            taskENTER_CRITICAL();
            bluetooth_ring_put(data[index]);
            taskEXIT_CRITICAL();
            (void)bluetooth_parse_byte(data[index]);
        }
    }
}

static ark_component_result_t bluetooth_init(void) {
    if (!bluetooth_load_config() ||
        (xTaskCreate(bluetooth_task, "bluetooth", BLUETOOTH_TASK_STACK_DEPTH,
                     NULL, tskIDLE_PRIORITY + 1U, &bluetooth_context.task) != pdPASS)) {
        return ARK_COMPONENT_ERROR;
    }
    bluetooth_context.ready = true;
    return ARK_COMPONENT_OK;
}

int32_t bluetooth_read(uint8_t *data, size_t size) {
    return bluetooth_context.ready ? bluetooth_ring_read(data, size) : -1;
}

void bluetooth_packet_reset(bluetooth_packet_builder_t *builder) {
    if (builder != NULL) {
        memset(builder, 0, sizeof(*builder));
    }
}

bool bluetooth_packet_add_uint8(bluetooth_packet_builder_t *builder, uint8_t value) {
    if ((builder == NULL) ||
        (builder->uint8_count >= BLUETOOTH_PACKET_UINT8_CAPACITY)) {
        return false;
    }
    builder->uint8_values[builder->uint8_count++] = value;
    return true;
}

bool bluetooth_packet_add_short(bluetooth_packet_builder_t *builder, int16_t value) {
    if ((builder == NULL) ||
        (builder->short_count >= BLUETOOTH_PACKET_SHORT_CAPACITY)) {
        return false;
    }
    builder->short_values[builder->short_count++] = value;
    return true;
}

bool bluetooth_packet_add_int(bluetooth_packet_builder_t *builder, int32_t value) {
    if ((builder == NULL) ||
        (builder->int_count >= BLUETOOTH_PACKET_INT_CAPACITY)) {
        return false;
    }
    builder->int_values[builder->int_count++] = value;
    return true;
}

bool bluetooth_packet_add_float(bluetooth_packet_builder_t *builder, float value) {
    if ((builder == NULL) ||
        (builder->float_count >= BLUETOOTH_PACKET_FLOAT_CAPACITY)) {
        return false;
    }
    builder->float_values[builder->float_count++] = value;
    return true;
}

bool bluetooth_packet_send(const bluetooth_packet_builder_t *builder) {
    size_t packet_index = 0U;
    size_t value_index;

    if ((builder == NULL) ||
        (builder->uint8_count > BLUETOOTH_PACKET_UINT8_CAPACITY) ||
        (builder->short_count > BLUETOOTH_PACKET_SHORT_CAPACITY) ||
        (builder->int_count > BLUETOOTH_PACKET_INT_CAPACITY) ||
        (builder->float_count > BLUETOOTH_PACKET_FLOAT_CAPACITY) ||
        !bluetooth_context.ready) {
        return false;
    }
    bluetooth_context.tx_packet[packet_index++] = BLUETOOTH_PACKET_HEADER;
    for (value_index = 0U; value_index < builder->uint8_count; ++value_index) {
        bluetooth_context.tx_packet[packet_index++] = builder->uint8_values[value_index];
    }
    for (value_index = 0U; value_index < builder->short_count; ++value_index) {
        uint16_t value = ark_uart_host_to_le16((uint16_t)builder->short_values[value_index]);

        memcpy(&bluetooth_context.tx_packet[packet_index], &value, sizeof(value));
        packet_index += sizeof(value);
    }
    for (value_index = 0U; value_index < builder->int_count; ++value_index) {
        uint32_t value = ark_uart_host_to_le32((uint32_t)builder->int_values[value_index]);

        memcpy(&bluetooth_context.tx_packet[packet_index], &value, sizeof(value));
        packet_index += sizeof(value);
    }
    for (value_index = 0U; value_index < builder->float_count; ++value_index) {
        uint32_t value = ark_uart_float_to_le(builder->float_values[value_index]);

        memcpy(&bluetooth_context.tx_packet[packet_index], &value, sizeof(value));
        packet_index += sizeof(value);
    }
    bluetooth_context.tx_packet[packet_index] = ark_uart_checksum_sum(
        &bluetooth_context.tx_packet[1], packet_index - 1U);
    ++packet_index;
    bluetooth_context.tx_packet[packet_index++] = BLUETOOTH_PACKET_TAIL;
    return ark_stream_write(bluetooth_context.stream, bluetooth_context.tx_packet,
                           packet_index, 100U) == (int32_t)packet_index;
}

bool bluetooth_get_control(bluetooth_control_t *control) {
    if (control == NULL) {
        return false;
    }
    taskENTER_CRITICAL();
    *control = bluetooth_context.control;
    taskEXIT_CRITICAL();
    return control->sequence != 0U;
}

static const ark_component_t bluetooth_component = {
    .name = "bluetooth",
    .init = bluetooth_init,
    .self_test = NULL,
    .self_test_expected_ms = 0U,
    .init_level = ARK_COMPONENT_INIT_LEVEL_3,
};

bool bluetooth_register(void) {
    return ark_component_register(&bluetooth_component);
}
