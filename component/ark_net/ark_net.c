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

#include "ark_net.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "led.h"
#include "task.h"
#include "wifi.h"
#include "ark_cli.h"
#include "ark_component.h"
#include "ark_dts.h"
#include "ark_net_protocol.h"

#define ARK_NET_HOST_SIZE 64U
#define ARK_NET_DEVICE_ID_SIZE 32U
#define ARK_NET_DEVICE_KEY_SIZE 96U
#define ARK_NET_LINE_SIZE 256U
#define ARK_NET_RX_SIZE 256U
#define ARK_NET_TASK_STACK_WORDS 640U
#define ARK_NET_CONNECT_TIMEOUT_MS 8000U
#define ARK_NET_HEARTBEAT_PERIOD_MS 5000U
#define ARK_NET_RECONNECT_DELAY_MS 1000U
#define ARK_NET_RECEIVE_TIMEOUT_MS 100U
#define ARK_NET_SELF_TEST_EXPECTED_MS 10U

#define ARK_NET_ERROR_NONE 0U
#define ARK_NET_ERROR_CONFIG 1U
#define ARK_NET_ERROR_CONNECT 2U
#define ARK_NET_ERROR_SEND 3U
#define ARK_NET_ERROR_PROTOCOL 4U

typedef struct {
    const char *host;
    const char *device_id;
    const char *device_key;
    uint16_t port;
    uint32_t upload_period_ms;
} ark_net_config_t;

typedef struct {
    ark_net_telemetry_t telemetry;
    ark_net_command_t command;
    char line[ARK_NET_LINE_SIZE];
    uint8_t response[ARK_NET_RX_SIZE];
    bool has_telemetry;
    bool session_ready;
    volatile bool publish_requested;
    volatile bool poll_requested;
    volatile uint32_t successful_request_count;
    volatile uint32_t failed_request_count;
    volatile uint16_t last_response_length;
    volatile uint8_t last_error;
    bool is_ready;
} ark_net_context_t;

static ark_net_config_t net_config;
static ark_net_context_t net_context;

static bool ark_net_load_config(void);
static bool ark_net_open_session(void);
static bool ark_net_send_packet(const char *packet, size_t size);
static bool ark_net_receive_state(uint32_t timeout_ms);
static bool ark_net_handle_state(const uint8_t *data, size_t size);
static void ark_net_task(void *argument);

static bool ark_net_load_config(void) {
    const ark_of_node_t *node =
        ark_of_find_compatible_node(NULL, "ark_net");
    uint32_t port;

    if ((node == NULL) ||
        (ark_of_property_read_string(node, "ark,host", &net_config.host) != 0) ||
        (ark_of_property_read_string(node, "ark,device-id", &net_config.device_id) != 0) ||
        (ark_of_property_read_string(node, "ark,device-password", &net_config.device_key) != 0) ||
        (ark_of_property_read_u32(node, "ark,port", &port) != 0)) {
        return false;
    }
    if ((port == 0U) || (port > UINT16_MAX) ||
        (strlen(net_config.host) >= ARK_NET_HOST_SIZE) ||
        (strlen(net_config.device_id) >= ARK_NET_DEVICE_ID_SIZE) ||
        (strlen(net_config.device_key) >= ARK_NET_DEVICE_KEY_SIZE)) {
        return false;
    }
    net_config.port = (uint16_t)port;
    net_config.upload_period_ms = ARK_NET_HEARTBEAT_PERIOD_MS;
    (void)ark_of_property_read_u32(
        node, "ark,upload-period-ms", &net_config.upload_period_ms);
    return net_config.upload_period_ms >= ARK_NET_HEARTBEAT_PERIOD_MS;
}

static bool ark_net_send_packet(const char *packet, size_t size) {
    return (packet != NULL) && (size != 0U) &&
           wifi_socket_send(
               (const uint8_t *)packet,
               size,
               ARK_NET_CONNECT_TIMEOUT_MS);
}

static bool ark_net_handle_state(const uint8_t *data, size_t size) {
    ark_net_command_t command;

    if ((data == NULL) || (size == 0U) || (size >= sizeof(net_context.response))) {
        return false;
    }
    if (!ark_net_protocol_decode_command(data, size, &command)) {
        return false;
    }
    net_context.command = command;
    (void)led_set_by_id(0U, command.led_on);
    return true;
}

static bool ark_net_receive_state(uint32_t timeout_ms) {
    size_t received = 0U;

    if (!wifi_socket_receive(
            net_context.response,
            sizeof(net_context.response),
            &received,
            timeout_ms)) {
        return false;
    }
    net_context.last_response_length = (uint16_t)received;
    if (!ark_net_handle_state(net_context.response, received)) {
        net_context.last_error = ARK_NET_ERROR_PROTOCOL;
        return false;
    }
    net_context.last_error = ARK_NET_ERROR_NONE;
    net_context.successful_request_count++;
    return true;
}

static bool ark_net_open_session(void) {
    size_t length;

    if (!wifi_connect() ||
        !wifi_socket_open(
            WIFI_SOCKET_TCP,
            net_config.host,
            net_config.port,
            ARK_NET_CONNECT_TIMEOUT_MS)) {
        net_context.last_error = ARK_NET_ERROR_CONNECT;
        return false;
    }
    if (!ark_net_protocol_encode_hello(
            net_config.device_id,
            net_config.device_key,
            net_context.line,
            sizeof(net_context.line),
            &length) ||
        !ark_net_send_packet(net_context.line, length) ||
        !ark_net_receive_state(ARK_NET_CONNECT_TIMEOUT_MS)) {
        wifi_socket_close();
        net_context.last_error = ARK_NET_ERROR_CONNECT;
        return false;
    }
    net_context.session_ready = true;
    return true;
}

bool ark_net_publish(void) {
    size_t length;

    if (!net_context.has_telemetry || !net_context.session_ready) {
        return false;
    }
    if (!ark_net_protocol_encode_telemetry(
            &net_context.telemetry,
            net_context.line,
            sizeof(net_context.line),
            &length) ||
        !ark_net_send_packet(net_context.line, length) ||
        !ark_net_receive_state(ARK_NET_CONNECT_TIMEOUT_MS)) {
        net_context.session_ready = false;
        wifi_socket_close();
        net_context.failed_request_count++;
        return false;
    }
    return true;
}

bool ark_net_poll(void) {
    size_t length;

    if (!net_context.session_ready ||
        !ark_net_protocol_encode_heartbeat(
            net_context.line,
            sizeof(net_context.line),
            &length) ||
        !ark_net_send_packet(net_context.line, length) ||
        !ark_net_receive_state(ARK_NET_CONNECT_TIMEOUT_MS)) {
        net_context.session_ready = false;
        wifi_socket_close();
        net_context.failed_request_count++;
        return false;
    }
    return true;
}

bool ark_net_set_telemetry(int32_t temperature_x10, int32_t humidity_x10) {
    net_context.telemetry.temperature_x10 = temperature_x10;
    net_context.telemetry.humidity_x10 = humidity_x10;
    ++net_context.telemetry.sequence;
    net_context.has_telemetry = true;
    return true;
}

bool ark_net_get_telemetry(ark_net_telemetry_t *telemetry) {
    if (telemetry == NULL) {
        return false;
    }
    *telemetry = net_context.telemetry;
    return net_context.has_telemetry;
}

bool ark_net_get_command(ark_net_command_t *command) {
    if (command == NULL) {
        return false;
    }
    *command = net_context.command;
    return net_context.is_ready;
}

bool ark_net_request_publish(void) {
    net_context.publish_requested = true;
    return true;
}

bool ark_net_request_poll(void) {
    net_context.poll_requested = true;
    return true;
}

static void ark_net_task(void *argument) {
    TickType_t last_heartbeat = 0U;
    TickType_t last_upload = 0U;

    (void)argument;
    for (;;) {
        TickType_t now = xTaskGetTickCount();

        if (!net_context.session_ready && !ark_net_open_session()) {
            vTaskDelay(pdMS_TO_TICKS(ARK_NET_RECONNECT_DELAY_MS));
            continue;
        }
        if (net_context.publish_requested ||
            (net_context.has_telemetry &&
             ((uint32_t)((now - last_upload) * portTICK_PERIOD_MS) >=
              net_config.upload_period_ms))) {
            net_context.publish_requested = false;
            if (ark_net_publish()) {
                last_upload = now;
            }
            continue;
        }
        if (net_context.poll_requested ||
            ((uint32_t)((now - last_heartbeat) * portTICK_PERIOD_MS) >=
             ARK_NET_HEARTBEAT_PERIOD_MS)) {
            net_context.poll_requested = false;
            if (ark_net_poll()) {
                last_heartbeat = now;
            }
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(ARK_NET_RECEIVE_TIMEOUT_MS));
    }
}

static ark_component_result_t ark_net_init(void) {
    if (!ark_net_load_config() || !wifi_is_ready()) {
        net_context.last_error = ARK_NET_ERROR_CONFIG;
        return ARK_COMPONENT_ERROR;
    }
    net_context.is_ready = true;
    if (xTaskCreate(ark_net_task, "arkNet", ARK_NET_TASK_STACK_WORDS,
                    NULL, tskIDLE_PRIORITY + 1U, NULL) != pdPASS) {
        net_context.is_ready = false;
        return ARK_COMPONENT_ERROR;
    }
    return ARK_COMPONENT_OK;
}

static ark_component_result_t ark_net_self_test(void) {
    return net_context.is_ready ? ARK_COMPONENT_OK : ARK_COMPONENT_ERROR;
}

#if ARK_DTS_HAS_UART_CLI
static int ark_net_cli(int argc, char *argv[]) {
    if ((argc == 2) && (strcmp(argv[1], "status") == 0)) {
        printf("net session=%u led=%u revision=%lu\r\n",
               net_context.session_ready ? 1U : 0U,
               net_context.command.led_on ? 1U : 0U,
               (unsigned long)net_context.command.revision);
        return 0;
    }
    if ((argc == 2) && (strcmp(argv[1], "diag") == 0)) {
        printf("net ok=%lu fail=%lu error=%u response=%u tcp=%u\r\n",
               (unsigned long)net_context.successful_request_count,
               (unsigned long)net_context.failed_request_count,
               (unsigned int)net_context.last_error,
               (unsigned int)net_context.last_response_length,
               wifi_socket_is_open() ? 1U : 0U);
        return 0;
    }
    if ((argc == 2) && (strcmp(argv[1], "telemetry") == 0)) {
        (void)ark_net_request_publish();
        return 0;
    }
    if ((argc == 2) && (strcmp(argv[1], "poll") == 0)) {
        (void)ark_net_request_poll();
        return 0;
    }
    printf("usage: net <status|diag|telemetry|poll>\r\n");
    return -1;
}

static ark_cli_command_t ark_net_command = {
    .name = "net",
    .usage = "net <status|diag|telemetry|poll>",
    .description = "show or request ARK TCP network actions",
    .handler = ark_net_cli,
};
#endif

static const ark_component_t ark_net_component = {
    .name = "ark_net",
    .init = ark_net_init,
    .self_test = ark_net_self_test,
    .self_test_expected_ms = ARK_NET_SELF_TEST_EXPECTED_MS,
    .init_level = ARK_COMPONENT_INIT_LEVEL_3,
};

bool ark_net_register(void) {
#if ARK_DTS_HAS_UART_CLI
    if (!ark_cli_register(&ark_net_command)) {
        return false;
    }
#endif
    return ark_component_register(&ark_net_component);
}
