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

#include "wifi.h"

#include "ark_component.h"
#include "ark_dts.h"
#include "wifi_backend.h"

typedef struct {
    const ark_of_node_t *node;
    const wifi_backend_t *backend;
    bool is_ready;
} wifi_context_t;

static wifi_context_t wifi_context;

static bool wifi_load_config(void) {
    const ark_of_node_t *node =
        ark_of_find_compatible_node(NULL, "wifi");

    wifi_context.node = node;
    return node != NULL;
}

bool wifi_backend_attach(const wifi_backend_t *backend) {
    if ((backend == NULL) || (backend->name == NULL) ||
        (backend->is_ready == NULL) || (backend->get_state == NULL) ||
        (backend->connect == NULL) || (backend->socket_open == NULL) ||
        (backend->socket_send == NULL) ||
        (backend->socket_receive == NULL) ||
        (backend->socket_close == NULL) ||
        (backend->socket_is_open == NULL) ||
        (wifi_context.backend != NULL)) {
        return false;
    }
    wifi_context.backend = backend;
    return true;
}

static ark_component_result_t wifi_init(void) {
    if (!wifi_load_config() || (wifi_context.backend == NULL) ||
        !wifi_context.backend->is_ready()) {
        return ARK_COMPONENT_ERROR;
    }
    wifi_context.is_ready = true;
    return ARK_COMPONENT_OK;
}

bool wifi_is_ready(void) {
    return wifi_context.is_ready && (wifi_context.backend != NULL) &&
           wifi_context.backend->is_ready();
}

wifi_state_t wifi_get_state(void) {
    return (wifi_context.backend == NULL) ?
           WIFI_STATE_OFFLINE : wifi_context.backend->get_state();
}

bool wifi_get_capabilities(wifi_capabilities_t *capabilities) {
    if ((capabilities == NULL) || (wifi_context.backend == NULL)) {
        return false;
    }
    *capabilities = wifi_context.backend->capabilities;
    return true;
}

bool wifi_connect(void) {
    return wifi_is_ready() && wifi_context.backend->connect();
}

void wifi_disconnect(void) {
    if ((wifi_context.backend != NULL) &&
        (wifi_context.backend->disconnect != NULL)) {
        wifi_context.backend->disconnect();
    }
}

bool wifi_socket_open(
    wifi_socket_protocol_t protocol,
    const char *host,
    uint16_t port,
    uint32_t timeout_ms) {
    bool supported;

    if (!wifi_is_ready() || (host == NULL) || (port == 0U) ||
        (timeout_ms == 0U)) {
        return false;
    }
    supported =
        ((protocol == WIFI_SOCKET_TCP) &&
         wifi_context.backend->capabilities.tcp) ||
        ((protocol == WIFI_SOCKET_UDP) &&
         wifi_context.backend->capabilities.udp) ||
        ((protocol == WIFI_SOCKET_TLS) &&
         wifi_context.backend->capabilities.tls);
    return supported && wifi_context.backend->socket_open(
                            protocol, host, port, timeout_ms);
}

bool wifi_socket_send(
    const uint8_t *data,
    size_t size,
    uint32_t timeout_ms) {
    return wifi_is_ready() && (data != NULL) && (size != 0U) &&
           wifi_context.backend->socket_send(data, size, timeout_ms);
}

bool wifi_socket_receive(
    uint8_t *data,
    size_t capacity,
    size_t *size,
    uint32_t timeout_ms) {
    return wifi_is_ready() && (data != NULL) && (capacity != 0U) &&
           (size != NULL) && wifi_context.backend->socket_receive(
                                 data, capacity, size, timeout_ms);
}

void wifi_socket_close(void) {
    if (wifi_context.backend != NULL) {
        wifi_context.backend->socket_close();
    }
}

bool wifi_socket_is_open(void) {
    return wifi_is_ready() && wifi_context.backend->socket_is_open();
}

static const ark_component_t wifi_component = {
    .name = "wifi",
    .init = wifi_init,
    .self_test = NULL,
    .self_test_expected_ms = 0U,
    .init_level = ARK_COMPONENT_INIT_LEVEL_3,
};

bool wifi_register(void) {
    return ark_component_register(&wifi_component);
}
