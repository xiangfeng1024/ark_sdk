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

#include "ark_net_protocol.h"

#include <stdio.h>
#include <string.h>

#include "json.h"

static bool ark_net_protocol_string_is_safe(const char *value) {
    const char *cursor;

    if ((value == NULL) || (value[0] == '\0')) {
        return false;
    }
    for (cursor = value; *cursor != '\0'; ++cursor) {
        if ((*cursor == '"') || (*cursor == '\\') ||
            ((uint8_t)*cursor < 0x20U)) {
            return false;
        }
    }
    return true;
}

bool ark_net_protocol_encode_hello(
    const char *device_id,
    const char *device_password,
    char *buffer,
    size_t capacity,
    size_t *length) {
    int result;

    if ((buffer == NULL) || (length == NULL) ||
        !ark_net_protocol_string_is_safe(device_id) ||
        !ark_net_protocol_string_is_safe(device_password)) {
        return false;
    }
    result = snprintf(
        buffer,
        capacity,
        "{\"type\":\"hello\",\"device_id\":\"%s\","
        "\"password\":\"%s\"}\n",
        device_id,
        device_password);
    if ((result <= 0) || ((size_t)result >= capacity)) {
        return false;
    }
    *length = (size_t)result;
    return true;
}

bool ark_net_protocol_encode_telemetry(
    const ark_net_telemetry_t *telemetry,
    char *buffer,
    size_t capacity,
    size_t *length) {
    int result;

    if ((telemetry == NULL) || (buffer == NULL) || (length == NULL)) {
        return false;
    }
    result = snprintf(
        buffer,
        capacity,
        "{\"type\":\"telemetry\",\"sequence\":%lu,"
        "\"temperature_x10\":%ld,\"humidity_x10\":%ld}\n",
        (unsigned long)telemetry->sequence,
        (long)telemetry->temperature_x10,
        (long)telemetry->humidity_x10);
    if ((result <= 0) || ((size_t)result >= capacity)) {
        return false;
    }
    *length = (size_t)result;
    return true;
}

bool ark_net_protocol_encode_heartbeat(
    char *buffer,
    size_t capacity,
    size_t *length) {
    static const char heartbeat[] = "{\"type\":\"heartbeat\"}\n";

    if ((buffer == NULL) || (length == NULL) ||
        (capacity < sizeof(heartbeat))) {
        return false;
    }
    memcpy(buffer, heartbeat, sizeof(heartbeat));
    *length = sizeof(heartbeat) - 1U;
    return true;
}

bool ark_net_protocol_decode_command(
    const uint8_t *data,
    size_t size,
    ark_net_command_t *command) {
    json_value_t *message;
    bool led_on;
    uint32_t revision;
    bool result = false;

    if ((data == NULL) || (size == 0U) || (command == NULL)) {
        return false;
    }
    message = json_parse((const char *)data, size);
    if (message == NULL) {
        return false;
    }
    if (json_get_bool(message, "led_on", &led_on) &&
        json_get_u32(message, "revision", &revision)) {
        command->led_on = led_on;
        command->revision = revision;
        result = true;
    }
    json_delete(message);
    return result;
}
