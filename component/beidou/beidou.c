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

#include "beidou.h"

#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "ark_component.h"
#include "ark_dts.h"
#include "ark_stream.h"
#include "ark_uart_codec.h"

#define BEIDOU_LINE_SIZE 96U
#define BEIDOU_TASK_STACK_DEPTH 192U
#define BEIDOU_NAME_SIZE 24U

typedef struct {
    ark_stream_t *stream;
    beidou_position_t position;
    TaskHandle_t task;
    const char *stream_name;
    bool ready;
} beidou_context_t;

static beidou_context_t beidou_context;

static bool beidou_load_config(void) {
    const ark_of_node_t *node = ark_of_find_compatible_node(NULL, "beidou");
    const ark_of_node_t *parent;
    const char *stream_name;

    if (node == NULL) {
        return false;
    }
    parent = ark_of_get_parent(node);
    if ((parent == NULL) ||
        (ark_of_property_read_string(parent, "ark,stream-name", &stream_name) != 0) ||
        (stream_name[0] == '\0')) {
        return false;
    }
    beidou_context.stream_name = stream_name;
    beidou_context.stream = ark_stream_find(stream_name);
    return beidou_context.stream != NULL;
}

static int beidou_hex(char character) {
    if ((character >= '0') && (character <= '9')) {
        return character - '0';
    }
    if ((character >= 'A') && (character <= 'F')) {
        return character - 'A' + 10;
    }
    if ((character >= 'a') && (character <= 'f')) {
        return character - 'a' + 10;
    }
    return -1;
}

static bool beidou_checksum(char *line) {
    char *asterisk;
    int high;
    int low;

    if ((line == NULL) || (line[0] != '$')) {
        return false;
    }
    asterisk = strchr(line, '*');
    if ((asterisk == NULL) ||
        (asterisk[1] == '\0') || (asterisk[2] == '\0')) {
        return false;
    }
    high = beidou_hex(asterisk[1]);
    low = beidou_hex(asterisk[2]);
    if ((high < 0) || (low < 0) ||
        (ark_uart_checksum_xor((const uint8_t *)&line[1],
                               (size_t)(asterisk - &line[1])) !=
         (uint8_t)((high << 4) | low))) {
        return false;
    }
    *asterisk = '\0';
    return true;
}

static size_t beidou_split(char *line, char *fields[], size_t capacity) {
    size_t count = 0U;
    char *cursor = line;

    while ((count < capacity) && (cursor != NULL)) {
        char *comma;

        fields[count++] = cursor;
        comma = strchr(cursor, ',');
        if (comma == NULL) {
            break;
        }
        *comma = '\0';
        cursor = comma + 1;
    }
    return count;
}

static bool beidou_coordinate(
    const char *text,
    char hemisphere,
    int32_t *value) {
    const char *dot;
    uint32_t whole;
    uint32_t fraction = 0U;
    uint32_t fraction_scale = 1U;
    uint32_t degrees;
    uint32_t minutes_x100000;
    int64_t coordinate;

    if ((text == NULL) || (text[0] == '\0') || (value == NULL)) {
        return false;
    }
    dot = strchr(text, '.');
    whole = (uint32_t)strtoul(text, NULL, 10);
    if (dot != NULL) {
        size_t index;

        for (index = 1U;
             (index <= 5U) && (dot[index] >= '0') && (dot[index] <= '9');
             ++index) {
            fraction = fraction * 10U + (uint32_t)(dot[index] - '0');
            fraction_scale *= 10U;
        }
    }
    while (fraction_scale < 100000U) {
        fraction *= 10U;
        fraction_scale *= 10U;
    }
    degrees = whole / 100U;
    minutes_x100000 = (whole % 100U) * 100000U + fraction;
    coordinate = (int64_t)degrees * 10000000LL +
        ((int64_t)minutes_x100000 * 10000000LL) / 6000000LL;
    if ((hemisphere == 'S') || (hemisphere == 'W')) {
        coordinate = -coordinate;
    }
    if ((coordinate > INT32_MAX) || (coordinate < INT32_MIN)) {
        return false;
    }
    *value = (int32_t)coordinate;
    return true;
}

static void beidou_parse_line(char *line) {
    char *fields[20];
    size_t count;
    size_t latitude_index;
    size_t longitude_index;
    size_t validity_index;
    bool valid;
    beidou_position_t next;

    if (!beidou_checksum(line)) {
        return;
    }
    count = beidou_split(line, fields, sizeof(fields) / sizeof(fields[0]));
    if ((count >= 7U) &&
        ((strcmp(fields[0], "$GNRMC") == 0) ||
         (strcmp(fields[0], "$GPRMC") == 0))) {
        validity_index = 2U;
        latitude_index = 3U;
        longitude_index = 5U;
        valid = fields[validity_index][0] == 'A';
    } else if ((count >= 7U) &&
               ((strcmp(fields[0], "$GNGGA") == 0) ||
                (strcmp(fields[0], "$GPGGA") == 0))) {
        validity_index = 6U;
        latitude_index = 2U;
        longitude_index = 4U;
        valid = fields[validity_index][0] > '0';
    } else {
        return;
    }
    memset(&next, 0, sizeof(next));
    next.valid = valid &&
        beidou_coordinate(fields[latitude_index], fields[latitude_index + 1U][0],
                          &next.latitude_e7) &&
        beidou_coordinate(fields[longitude_index], fields[longitude_index + 1U][0],
                          &next.longitude_e7);
    next.tick = xTaskGetTickCount();
    taskENTER_CRITICAL();
    beidou_context.position = next;
    taskEXIT_CRITICAL();
}

static void beidou_task(void *argument) {
    char line[BEIDOU_LINE_SIZE];
    size_t length = 0U;

    (void)argument;
    for (;;) {
        uint8_t byte;

        if (ark_stream_read(beidou_context.stream, &byte, 1U,
                           ARK_STREAM_WAIT_FOREVER) != 1) {
            continue;
        }
        if ((byte == '\r') || (byte == '\n')) {
            if (length > 0U) {
                line[length] = '\0';
                beidou_parse_line(line);
                length = 0U;
            }
        } else if (length < (sizeof(line) - 1U)) {
            line[length++] = (char)byte;
        } else {
            length = 0U;
        }
    }
}

static ark_component_result_t beidou_init(void) {
    if (!beidou_load_config() ||
        (xTaskCreate(beidou_task, "beidou", BEIDOU_TASK_STACK_DEPTH,
                     NULL, tskIDLE_PRIORITY + 1U, &beidou_context.task) != pdPASS)) {
        return ARK_COMPONENT_ERROR;
    }
    beidou_context.ready = true;
    return ARK_COMPONENT_OK;
}

bool beidou_get_position(beidou_position_t *position) {
    if (position == NULL) {
        return false;
    }
    taskENTER_CRITICAL();
    *position = beidou_context.position;
    taskEXIT_CRITICAL();
    return position->tick != 0U;
}

static const ark_component_t beidou_component = {
    .name = "beidou",
    .init = beidou_init,
    .self_test = NULL,
    .self_test_expected_ms = 0U,
    .init_level = ARK_COMPONENT_INIT_LEVEL_3,
};

bool beidou_register(void) {
    return ark_component_register(&beidou_component);
}
