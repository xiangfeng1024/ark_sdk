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

#include "maixcam.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "ark_component.h"
#include "ark_dts.h"
#include "ark_stream.h"

#define MAIXCAM_HEADER_1 0x5AU
#define MAIXCAM_HEADER_2 0x5BU
#define MAIXCAM_TAIL 0xA5U
#define MAIXCAM_FLAG_DETECTED 0x01U
#define MAIXCAM_MIN_PACKET_SIZE 5U
#define MAIXCAM_FLOWER_PACKET_SIZE 14U
#define MAIXCAM_MAX_PACKET_SIZE (MAIXCAM_MAX_PAYLOAD_SIZE + 6U)
#define MAIXCAM_RING_SIZE 128U
#define MAIXCAM_TASK_STACK_DEPTH 128U

typedef enum {
    MAIXCAM_WAIT_HEADER_1 = 0,
    MAIXCAM_WAIT_HEADER_2,
    MAIXCAM_WAIT_LENGTH,
    MAIXCAM_READ_PACKET
} maixcam_parser_state_t;

typedef struct {
    maixcam_parser_state_t state;
    uint8_t packet[MAIXCAM_MAX_PACKET_SIZE];
    uint8_t length;
    uint8_t index;
    uint32_t valid_packets;
    uint32_t invalid_packets;
} maixcam_parser_t;

typedef struct {
    uint8_t data[MAIXCAM_RING_SIZE];
    uint16_t read;
    uint16_t write;
} maixcam_ring_t;

typedef struct {
    ark_stream_t *stream;
    maixcam_ring_t ring;
    maixcam_parser_t parser;
    maixcam_result_t result;
    maixcam_command_request_t command;
    uint8_t response[MAIXCAM_MAX_PACKET_SIZE];
    TaskHandle_t task;
    bool ready;
} maixcam_context_t;

static maixcam_context_t maixcam_context;

static bool maixcam_load_config(void) {
    const ark_of_node_t *node = ark_of_find_compatible_node(NULL, "maixcam");
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
    maixcam_context.stream = ark_stream_find(stream_name);
    return maixcam_context.stream != NULL;
}

static uint16_t maixcam_u16(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static void maixcam_resync(maixcam_parser_t *parser, uint8_t byte) {
    parser->index = 0U;
    if (byte == MAIXCAM_HEADER_1) {
        parser->packet[0] = byte;
        parser->index = 1U;
        parser->state = MAIXCAM_WAIT_HEADER_2;
    } else {
        parser->state = MAIXCAM_WAIT_HEADER_1;
    }
}

static bool maixcam_decode(const uint8_t *packet, uint8_t length) {
    uint8_t command;

    if ((length < MAIXCAM_MIN_PACKET_SIZE) ||
        (packet[length - 1U] != MAIXCAM_TAIL)) {
        return false;
    }
    command = packet[3];
    if (command == MAIXCAM_COMMAND_FLOWER) {
        maixcam_result_t result;
        uint8_t flags = packet[4];

        if ((length != MAIXCAM_FLOWER_PACKET_SIZE) ||
            ((flags & (uint8_t)~MAIXCAM_FLAG_DETECTED) != 0U)) {
            return false;
        }
        memset(&result, 0, sizeof(result));
        result.detected = (flags & MAIXCAM_FLAG_DETECTED) != 0U;
        result.center_x = maixcam_u16(&packet[5]);
        result.center_y = maixcam_u16(&packet[7]);
        result.width = maixcam_u16(&packet[9]);
        result.height = maixcam_u16(&packet[11]);
        if (!result.detected &&
            ((result.center_x != 0U) || (result.center_y != 0U) ||
             (result.width != 0U) || (result.height != 0U))) {
            return false;
        }
        if (result.detected &&
            ((result.width == 0U) || (result.height == 0U) ||
             (result.center_x < (result.width / 2U)) ||
             (result.center_y < (result.height / 2U)))) {
            return false;
        }
        result.tick = xTaskGetTickCount();
        taskENTER_CRITICAL();
        maixcam_context.result = result;
        taskEXIT_CRITICAL();
        return true;
    }
    if ((length != MAIXCAM_MIN_PACKET_SIZE) ||
        (command < MAIXCAM_COMMAND_CAL_WHITE) ||
        (command > MAIXCAM_COMMAND_START_TASK)) {
        return false;
    }
    taskENTER_CRITICAL();
    maixcam_context.command.command = command;
    maixcam_context.command.sequence++;
    maixcam_context.command.tick = xTaskGetTickCount();
    taskEXIT_CRITICAL();
    return true;
}

static bool maixcam_parse_byte(uint8_t byte) {
    maixcam_parser_t *parser = &maixcam_context.parser;

    switch (parser->state) {
    case MAIXCAM_WAIT_HEADER_1:
        maixcam_resync(parser, byte);
        break;
    case MAIXCAM_WAIT_HEADER_2:
        if (byte == MAIXCAM_HEADER_2) {
            parser->packet[parser->index++] = byte;
            parser->state = MAIXCAM_WAIT_LENGTH;
        } else {
            maixcam_resync(parser, byte);
        }
        break;
    case MAIXCAM_WAIT_LENGTH:
        if ((byte >= MAIXCAM_MIN_PACKET_SIZE) &&
            (byte <= MAIXCAM_MAX_PACKET_SIZE)) {
            parser->packet[parser->index++] = byte;
            parser->length = byte;
            parser->state = MAIXCAM_READ_PACKET;
        } else {
            maixcam_resync(parser, byte);
        }
        break;
    case MAIXCAM_READ_PACKET:
        parser->packet[parser->index++] = byte;
        if (parser->index == parser->length) {
            bool valid = maixcam_decode(parser->packet, parser->length);

            if (valid) {
                parser->valid_packets++;
            } else {
                parser->invalid_packets++;
            }
            maixcam_resync(parser, byte);
            return valid;
        }
        break;
    default:
        memset(parser, 0, sizeof(*parser));
        break;
    }
    return false;
}

static void maixcam_ring_put(uint8_t byte) {
    uint16_t next = (uint16_t)((maixcam_context.ring.write + 1U) % MAIXCAM_RING_SIZE);

    if (next == maixcam_context.ring.read) {
        maixcam_context.ring.read = (uint16_t)(
            (maixcam_context.ring.read + 1U) % MAIXCAM_RING_SIZE);
    }
    maixcam_context.ring.data[maixcam_context.ring.write] = byte;
    maixcam_context.ring.write = next;
}

static int32_t maixcam_ring_read(uint8_t *data, size_t size) {
    size_t count = 0U;

    if ((data == NULL) || (size == 0U)) {
        return -1;
    }
    taskENTER_CRITICAL();
    while ((count < size) &&
           (maixcam_context.ring.read != maixcam_context.ring.write)) {
        data[count++] = maixcam_context.ring.data[maixcam_context.ring.read];
        maixcam_context.ring.read = (uint16_t)(
            (maixcam_context.ring.read + 1U) % MAIXCAM_RING_SIZE);
    }
    taskEXIT_CRITICAL();
    return (int32_t)count;
}

static void maixcam_task(void *argument) {
    uint8_t data[32];

    (void)argument;
    for (;;) {
        int32_t size = ark_stream_read(
            maixcam_context.stream, data, sizeof(data), ARK_STREAM_WAIT_FOREVER);
        int32_t index;

        if (size <= 0) {
            continue;
        }
        for (index = 0; index < size; ++index) {
            taskENTER_CRITICAL();
            maixcam_ring_put(data[index]);
            taskEXIT_CRITICAL();
            (void)maixcam_parse_byte(data[index]);
        }
    }
}

static ark_component_result_t maixcam_init(void) {
    if (!maixcam_load_config() ||
        (xTaskCreate(maixcam_task, "maixcam", MAIXCAM_TASK_STACK_DEPTH,
                     NULL, tskIDLE_PRIORITY + 1U, &maixcam_context.task) != pdPASS)) {
        return ARK_COMPONENT_ERROR;
    }
    maixcam_context.ready = true;
    return ARK_COMPONENT_OK;
}

bool maixcam_get_result(maixcam_result_t *result) {
    if (result == NULL) {
        return false;
    }
    taskENTER_CRITICAL();
    *result = maixcam_context.result;
    taskEXIT_CRITICAL();
    return result->tick != 0U;
}

bool maixcam_get_command(maixcam_command_request_t *request) {
    if (request == NULL) {
        return false;
    }
    taskENTER_CRITICAL();
    *request = maixcam_context.command;
    taskEXIT_CRITICAL();
    return request->sequence != 0U;
}

bool maixcam_send_response(
    uint8_t command,
    uint8_t status,
    const void *payload,
    size_t payload_size) {
    size_t packet_size = payload_size + 6U;

    if (!maixcam_context.ready ||
        (command < MAIXCAM_COMMAND_CAL_WHITE) ||
        (command > MAIXCAM_COMMAND_TASK_READY) ||
        (payload_size > MAIXCAM_MAX_PAYLOAD_SIZE) ||
        ((payload == NULL) && (payload_size != 0U))) {
        return false;
    }
    maixcam_context.response[0] = MAIXCAM_HEADER_1;
    maixcam_context.response[1] = MAIXCAM_HEADER_2;
    maixcam_context.response[2] = (uint8_t)packet_size;
    maixcam_context.response[3] = command;
    maixcam_context.response[4] = status;
    if (payload_size != 0U) {
        memcpy(&maixcam_context.response[5], payload, payload_size);
    }
    maixcam_context.response[packet_size - 1U] = MAIXCAM_TAIL;
    return ark_stream_write(maixcam_context.stream, maixcam_context.response,
                           packet_size, 100U) == (int32_t)packet_size;
}

bool maixcam_notify_task_ready(void) {
    return maixcam_send_response(
        MAIXCAM_COMMAND_TASK_READY, MAIXCAM_STATUS_OK, NULL, 0U);
}

int32_t maixcam_read_raw(uint8_t *data, size_t size) {
    return maixcam_context.ready ? maixcam_ring_read(data, size) : -1;
}

static const ark_component_t maixcam_component = {
    .name = "maixcam",
    .init = maixcam_init,
    .self_test = NULL,
    .self_test_expected_ms = 0U,
    .init_level = ARK_COMPONENT_INIT_LEVEL_3,
};

bool maixcam_register(void) {
    return ark_component_register(&maixcam_component);
}
