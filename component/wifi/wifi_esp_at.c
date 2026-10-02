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

#include "wifi_esp_at.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "ark_cli.h"
#include "ark_component.h"
#include "ark_dts.h"
#include "ark_hal_uart.h"
#include "ark_stream.h"
#include "wifi.h"
#include "wifi_backend.h"

#define WIFI_ESP_AT_RX_RING_SIZE 1024U
#define WIFI_ESP_AT_RX_POLL_MS 2U
#define WIFI_ESP_AT_COMMAND_SIZE 192U
#define WIFI_ESP_AT_RESPONSE_SIZE 256U
#define WIFI_ESP_AT_DIAGNOSTIC_SIZE 96U
#define WIFI_ESP_AT_AT_TIMEOUT_MS 1200U
#define WIFI_ESP_AT_JOIN_TIMEOUT_MS 20000U
#define WIFI_ESP_AT_BUSY_RETRY_COUNT 8U
#define WIFI_ESP_AT_BUSY_RETRY_DELAY_MS 500U
#define WIFI_ESP_AT_SELF_TEST_EXPECTED_MS 1500U
#define WIFI_ESP_AT_BOOT_SETTLE_MS 3000U
#define WIFI_ESP_AT_RX_TASK_STACK_DEPTH 128U

typedef struct {
    ark_hal_uart_id_t uart_id;
    uint32_t baud_rate;
    const char *ssid;
    const char *password;
    bool udp_enabled;
    bool tls_enabled;
} wifi_esp_at_config_t;

typedef struct {
    ark_stream_t *stream;
    uint8_t rx_ring[WIFI_ESP_AT_RX_RING_SIZE];
    volatile uint16_t rx_head;
    volatile uint16_t rx_tail;
    StaticSemaphore_t request_mutex_control;
    SemaphoreHandle_t request_mutex;
    volatile uint32_t rx_byte_count;
    volatile uint32_t rx_error_count;
    char last_connect_failure[WIFI_ESP_AT_DIAGNOSTIC_SIZE];
    bool is_ready;
    bool tcp_connected;
    wifi_state_t state;
    TaskHandle_t rx_task;
} wifi_esp_at_context_t;

static wifi_esp_at_context_t wifi_context = {
    .state = WIFI_STATE_OFFLINE,
};
static wifi_esp_at_config_t wifi_config;

static bool wifi_esp_at_load_config(void);
static bool wifi_esp_at_command(
    const char *command,
    const char *success_token,
    uint32_t timeout_ms,
    char *response,
    size_t response_capacity);
static bool wifi_esp_at_connect_internal(
    char *diagnostic,
    size_t diagnostic_capacity);
static void wifi_esp_at_connect_diagnostic_set(
    char *diagnostic,
    size_t diagnostic_capacity,
    const char *stage,
    const char *response);
static void wifi_esp_at_connect_failure_set(
    const char *stage,
    const char *response);
static void wifi_esp_at_transaction_failure_set(
    const char *stage,
    const char *response);
static bool wifi_esp_at_validate_at_string(const char *value);
static bool wifi_esp_at_rx_pop(uint8_t *byte);
static bool wifi_esp_at_response_is_link_error(const char *response);
static void wifi_esp_at_rx_task(void *argument);
#if ARK_DTS_HAS_UART_CLI
static size_t wifi_esp_at_response_length(const char *response, size_t capacity);
static void wifi_esp_at_cli_print_response(const char *response, size_t capacity);
#endif

static bool wifi_esp_at_load_config(void) {
    const ark_of_node_t *node =
        ark_of_find_compatible_node(NULL, "wifi_esp_at");
    const ark_of_node_t *parent;
    uint8_t uart_id;
    uint32_t baud_rate;
    const char *stream_name = NULL;

    if ((node == NULL) ||
        (ark_of_property_read_string(node, "ark,ssid", &wifi_config.ssid) != 0) ||
        (ark_of_property_read_string(node, "ark,password", &wifi_config.password) != 0)) {
        return false;
    }

    if (ark_of_property_read_string(node, "ark,stream-name", &stream_name) != 0) {
        parent = ark_of_get_parent(node);
        if ((parent == NULL) ||
            (ark_of_property_read_string(parent, "ark,stream-name", &stream_name) != 0)) {
            return false;
        }
    }

    parent = ark_of_get_parent(node);
    if ((ark_of_property_read_u32(node, "current-speed", &baud_rate) != 0) &&
        ((parent == NULL) ||
         (ark_of_property_read_u32(parent, "current-speed", &baud_rate) != 0))) {
        return false;
    }
    if ((baud_rate == 0U) ||
        !wifi_esp_at_validate_at_string(wifi_config.ssid) ||
        !wifi_esp_at_validate_at_string(wifi_config.password)) {
        return false;
    }

    wifi_context.stream = ark_stream_find(stream_name);
    if (wifi_context.stream == NULL) {
        return false;
    }
    if (ark_of_hal_get_parent(node, ARK_OF_HAL_UART, &uart_id) != 0) {
        return false;
    }
    wifi_config.uart_id = (ark_hal_uart_id_t)uart_id;
    wifi_config.baud_rate = baud_rate;
    wifi_config.udp_enabled =
        ark_of_property_read_bool(node, "ark,udp-enabled");
    wifi_config.tls_enabled =
        ark_of_property_read_bool(node, "ark,tls-enabled");
    return true;
}

static bool wifi_esp_at_validate_at_string(const char *value) {
    const char *cursor;

    if ((value == NULL) || (value[0] == '\0')) {
        return false;
    }
    for (cursor = value; *cursor != '\0'; ++cursor) {
        if ((*cursor == '"') || (*cursor == '\r') || (*cursor == '\n')) {
            return false;
        }
    }
    return true;
}

static bool wifi_esp_at_response_is_link_error(const char *response) {
    return (response == NULL) ||
           (strstr(response, "ERROR") != NULL) ||
           (strstr(response, "FAIL") != NULL) ||
           (strstr(response, "busy") != NULL) ||
           (strstr(response, "CLOSED") != NULL) ||
           (strstr(response, "link is not valid") != NULL);
}

static void wifi_esp_at_rx_task(void *argument) {
    uint8_t data[32];

    (void)argument;
    for (;;) {
        int32_t size = ark_stream_read(
            wifi_context.stream, data, sizeof(data), ARK_STREAM_WAIT_FOREVER);
        int32_t index;

        if (size <= 0) {
            wifi_context.rx_error_count++;
            continue;
        }
        for (index = 0; index < size; ++index) {
            uint16_t next_head;

            taskENTER_CRITICAL();
            next_head = (uint16_t)((wifi_context.rx_head + 1U) %
                                   WIFI_ESP_AT_RX_RING_SIZE);
            if (next_head != wifi_context.rx_tail) {
                wifi_context.rx_ring[wifi_context.rx_head] = data[index];
                wifi_context.rx_head = next_head;
                wifi_context.rx_byte_count++;
            } else {
                wifi_context.rx_error_count++;
            }
            taskEXIT_CRITICAL();
        }
    }
}

static void wifi_esp_at_rx_clear(void) {
    wifi_context.rx_tail = wifi_context.rx_head;
}

static bool wifi_esp_at_rx_pop(uint8_t *byte) {
    uint16_t tail;

    if (byte == NULL) {
        return false;
    }
    taskENTER_CRITICAL();
    if (wifi_context.rx_tail == wifi_context.rx_head) {
        taskEXIT_CRITICAL();
        return false;
    }
    tail = wifi_context.rx_tail;
    *byte = wifi_context.rx_ring[tail];
    wifi_context.rx_tail = (uint16_t)((tail + 1U) % WIFI_ESP_AT_RX_RING_SIZE);
    taskEXIT_CRITICAL();
    return true;
}

static bool wifi_esp_at_read_until(
    const char *success_token,
    uint32_t timeout_ms,
    char *response,
    size_t response_capacity,
    size_t *response_length) {
    TickType_t started = xTaskGetTickCount();
    size_t length = 0U;
    uint8_t byte;

    if ((success_token == NULL) || (response == NULL) ||
        (response_capacity < 2U) || (response_length == NULL)) {
        return false;
    }

    response[0] = '\0';
    while ((uint32_t)((xTaskGetTickCount() - started) * portTICK_PERIOD_MS) < timeout_ms) {
        if (!wifi_esp_at_rx_pop(&byte)) {
            vTaskDelay(pdMS_TO_TICKS(WIFI_ESP_AT_RX_POLL_MS));
            continue;
        }
        if (length + 1U >= response_capacity) {
            response[response_capacity - 1U] = '\0';
            *response_length = response_capacity - 1U;
            return false;
        }
        response[length] = (char)byte;
        ++length;
        response[length] = '\0';
        if (wifi_esp_at_response_is_link_error(response)) {
            *response_length = length;
            return false;
        }
        if (strstr(response, success_token) != NULL) {
            *response_length = length;
            return true;
        }
    }

    *response_length = length;
    return false;
}

#if ARK_DTS_HAS_UART_CLI
static size_t wifi_esp_at_response_length(const char *response, size_t capacity) {
    size_t length = 0U;

    if (response == NULL) {
        return 0U;
    }
    while ((length < capacity) && (response[length] != '\0')) {
        ++length;
    }
    return length;
}
#endif

static bool wifi_esp_at_command(
    const char *command,
    const char *success_token,
    uint32_t timeout_ms,
    char *response,
    size_t response_capacity) {
    size_t command_length;
    size_t response_length;
    uint32_t attempt;

    if (!wifi_context.is_ready || (command == NULL) || (success_token == NULL)) {
        return false;
    }

    command_length = strlen(command);
    if ((command_length == 0U) || (command_length > UINT16_MAX)) {
        return false;
    }
    for (attempt = 0U; attempt < WIFI_ESP_AT_BUSY_RETRY_COUNT; ++attempt) {
        wifi_esp_at_rx_clear();
        if (ark_stream_write(
                wifi_context.stream,
                (const uint8_t *)command,
                command_length,
                timeout_ms) != (int32_t)command_length) {
            wifi_context.state = WIFI_STATE_ERROR;
            return false;
        }
        if (wifi_esp_at_read_until(
                success_token, timeout_ms, response, response_capacity,
                &response_length)) {
            return true;
        }
        if ((strstr(response, "busy") == NULL) ||
            (attempt + 1U >= WIFI_ESP_AT_BUSY_RETRY_COUNT)) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(WIFI_ESP_AT_BUSY_RETRY_DELAY_MS));
    }
    return false;
}

static void wifi_esp_at_connect_diagnostic_set(
    char *diagnostic,
    size_t diagnostic_capacity,
    const char *stage,
    const char *response) {
    if ((stage == NULL) || (response == NULL)) {
        return;
    }
    if ((diagnostic == NULL) || (diagnostic_capacity < 2U)) {
        return;
    }
    snprintf(diagnostic, diagnostic_capacity, "%s: %s", stage, response);
}

static void wifi_esp_at_connect_failure_set(
    const char *stage,
    const char *response) {
    if ((stage == NULL) || (response == NULL)) {
        return;
    }
    snprintf(wifi_context.last_connect_failure,
             sizeof(wifi_context.last_connect_failure),
             "%s: %s",
             stage,
             response);
}

static void wifi_esp_at_transaction_failure_set(
    const char *stage,
    const char *response) {
    wifi_esp_at_connect_failure_set(stage, response);
}

static bool wifi_esp_at_connect_internal(
    char *diagnostic,
    size_t diagnostic_capacity) {
    char command[WIFI_ESP_AT_COMMAND_SIZE];
    char response[WIFI_ESP_AT_RESPONSE_SIZE] = {0};
    int length;
    bool connected = false;

    if ((diagnostic != NULL) && (diagnostic_capacity > 0U)) {
        diagnostic[0] = '\0';
    }
    if (!wifi_context.is_ready || (wifi_context.request_mutex == NULL) ||
        (xSemaphoreTake(
             wifi_context.request_mutex,
             pdMS_TO_TICKS(WIFI_ESP_AT_JOIN_TIMEOUT_MS)) != pdTRUE)) {
        wifi_esp_at_connect_diagnostic_set(
            diagnostic, diagnostic_capacity, "lock", "busy");
        wifi_esp_at_connect_failure_set("lock", "busy");
        return false;
    }

    wifi_context.state = WIFI_STATE_READY;
    wifi_esp_at_connect_diagnostic_set(
        diagnostic, diagnostic_capacity, "AT", "waiting");
    if (!wifi_esp_at_command("AT\r\n", "OK", WIFI_ESP_AT_AT_TIMEOUT_MS,
                            response, sizeof(response))) {
        wifi_esp_at_connect_diagnostic_set(
            diagnostic, diagnostic_capacity, "AT", response);
        wifi_esp_at_connect_failure_set("AT", response);
        wifi_context.state = WIFI_STATE_ERROR;
    } else {
        wifi_esp_at_connect_diagnostic_set(
            diagnostic, diagnostic_capacity, "ATE0", "waiting");
    }
    if ((wifi_context.state != WIFI_STATE_ERROR) &&
        !wifi_esp_at_command("ATE0\r\n", "OK", WIFI_ESP_AT_AT_TIMEOUT_MS,
                            response, sizeof(response))) {
        wifi_esp_at_connect_diagnostic_set(
            diagnostic, diagnostic_capacity, "ATE0", response);
        wifi_esp_at_connect_failure_set("ATE0", response);
        wifi_context.state = WIFI_STATE_ERROR;
    } else if (wifi_context.state != WIFI_STATE_ERROR) {
        wifi_esp_at_connect_diagnostic_set(
            diagnostic, diagnostic_capacity, "CWMODE", "waiting");
    }
    if ((wifi_context.state != WIFI_STATE_ERROR) &&
        !wifi_esp_at_command("AT+CWMODE=1\r\n", "OK", WIFI_ESP_AT_AT_TIMEOUT_MS,
                            response, sizeof(response))) {
        wifi_esp_at_connect_diagnostic_set(
            diagnostic, diagnostic_capacity, "CWMODE", response);
        wifi_esp_at_connect_failure_set("CWMODE", response);
        wifi_context.state = WIFI_STATE_ERROR;
    } else if (wifi_context.state != WIFI_STATE_ERROR) {
        length = snprintf(command, sizeof(command), "AT+CWJAP=\"%s\",\"%s\"\r\n",
                          wifi_config.ssid, wifi_config.password);
        wifi_esp_at_connect_diagnostic_set(
            diagnostic, diagnostic_capacity, "CWJAP", "waiting");
        if ((length > 0) && ((size_t)length < sizeof(command)) &&
            wifi_esp_at_command(command, "OK", WIFI_ESP_AT_JOIN_TIMEOUT_MS,
                               response, sizeof(response))) {
            wifi_context.state = WIFI_STATE_CONNECTED;
            wifi_esp_at_connect_diagnostic_set(
                diagnostic, diagnostic_capacity, "CWJAP", "connected");
            connected = true;
        } else if (strstr(response, "WIFI GOT IP") != NULL) {
            wifi_context.state = WIFI_STATE_CONNECTED;
            wifi_esp_at_connect_diagnostic_set(
                diagnostic, diagnostic_capacity, "CWJAP", "got IP");
            connected = true;
        } else {
            wifi_esp_at_connect_diagnostic_set(
                diagnostic, diagnostic_capacity, "CWJAP", response);
            wifi_esp_at_connect_failure_set("CWJAP", response);
            wifi_context.state = WIFI_STATE_ERROR;
        }
    }

    (void)xSemaphoreGive(wifi_context.request_mutex);
    return connected;
}

static bool wifi_esp_at_connect(void) {
    return wifi_esp_at_connect_internal(NULL, 0U);
}

static bool wifi_esp_at_socket_open(
    wifi_socket_protocol_t protocol,
    const char *host,
    uint16_t port,
    uint32_t timeout_ms) {
    char command[WIFI_ESP_AT_COMMAND_SIZE];
    char response[WIFI_ESP_AT_RESPONSE_SIZE] = {0};
    const char *protocol_name;
    int length;
    bool connected = false;

    if (protocol == WIFI_SOCKET_TCP) {
        protocol_name = "TCP";
    } else if ((protocol == WIFI_SOCKET_UDP) && wifi_config.udp_enabled) {
        protocol_name = "UDP";
    } else if ((protocol == WIFI_SOCKET_TLS) && wifi_config.tls_enabled) {
        protocol_name = "SSL";
    } else {
        return false;
    }
    if ((host == NULL) || (host[0] == '\0') || (port == 0U) ||
        (timeout_ms == 0U)) {
        return false;
    }
    if (wifi_context.tcp_connected) {
        return true;
    }
    if ((wifi_context.state != WIFI_STATE_CONNECTED) &&
        !wifi_esp_at_connect()) {
        return false;
    }
    if (xSemaphoreTake(wifi_context.request_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        wifi_esp_at_transaction_failure_set("socket lock", "busy");
        return false;
    }

    /*
     * Power loss and software reset can leave a stale single socket in the
     * AT stack. A best-effort close prevents it from blocking the new session.
     */
    (void)wifi_esp_at_command(
        "AT+CIPMUX=0\r\n", "OK", WIFI_ESP_AT_AT_TIMEOUT_MS,
        response, sizeof(response));
    (void)wifi_esp_at_command(
        "AT+CIPCLOSE\r\n", "OK", WIFI_ESP_AT_AT_TIMEOUT_MS,
        response, sizeof(response));
    length = snprintf(command, sizeof(command),
                      "AT+CIPSTART=\"%s\",\"%s\",%u\r\n",
                      protocol_name, host, (unsigned int)port);
    if ((length > 0) && ((size_t)length < sizeof(command)) &&
        wifi_esp_at_command(command, "OK", timeout_ms, response, sizeof(response))) {
        wifi_context.tcp_connected = true;
        connected = true;
    } else {
        wifi_context.tcp_connected = false;
        wifi_context.state = WIFI_STATE_ERROR;
        wifi_esp_at_transaction_failure_set("CIPSTART", response);
    }
    (void)xSemaphoreGive(wifi_context.request_mutex);
    return connected;
}

static void wifi_esp_at_socket_close(void) {
    char response[WIFI_ESP_AT_RESPONSE_SIZE] = {0};

    if (!wifi_context.is_ready || (wifi_context.request_mutex == NULL)) {
        return;
    }
    if (xSemaphoreTake(wifi_context.request_mutex,
                       pdMS_TO_TICKS(WIFI_ESP_AT_AT_TIMEOUT_MS)) == pdTRUE) {
        (void)wifi_esp_at_command(
            "AT+CIPCLOSE\r\n", "OK", WIFI_ESP_AT_AT_TIMEOUT_MS,
            response, sizeof(response));
        wifi_context.tcp_connected = false;
        (void)xSemaphoreGive(wifi_context.request_mutex);
    }
}

static bool wifi_esp_at_socket_is_open(void) {
    return wifi_context.tcp_connected;
}

static bool wifi_esp_at_socket_send(
    const uint8_t *data,
    size_t size,
    uint32_t timeout_ms) {
    char command[WIFI_ESP_AT_COMMAND_SIZE];
    char response[WIFI_ESP_AT_RESPONSE_SIZE] = {0};
    size_t response_length = 0U;
    int length;
    bool sent = false;

    if ((data == NULL) || (size == 0U) || (size > UINT16_MAX) ||
        !wifi_context.tcp_connected || (timeout_ms == 0U)) {
        return false;
    }
    if (xSemaphoreTake(wifi_context.request_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return false;
    }
    length = snprintf(command, sizeof(command), "AT+CIPSEND=%lu\r\n",
                      (unsigned long)size);
    if ((length > 0) && ((size_t)length < sizeof(command)) &&
        wifi_esp_at_command(command, ">", timeout_ms, response, sizeof(response)) &&
        (ark_stream_write(wifi_context.stream, data, size, timeout_ms) ==
         (int32_t)size) &&
        wifi_esp_at_read_until("SEND OK", timeout_ms, response, sizeof(response),
                              &response_length)) {
        sent = true;
    } else {
        wifi_esp_at_transaction_failure_set("CIPSEND", response);
        wifi_context.tcp_connected = false;
        wifi_context.state = WIFI_STATE_ERROR;
    }
    (void)xSemaphoreGive(wifi_context.request_mutex);
    return sent;
}

static bool wifi_esp_at_socket_receive(
    uint8_t *data,
    size_t capacity,
    size_t *size,
    uint32_t timeout_ms) {
    TickType_t started = xTaskGetTickCount();
    char header[24] = {0};
    size_t header_length = 0U;
    size_t payload_length = 0U;
    size_t received = 0U;
    uint8_t byte;

    if ((data == NULL) || (capacity == 0U) || (size == NULL) ||
        !wifi_context.tcp_connected || (timeout_ms == 0U)) {
        return false;
    }
    *size = 0U;
    if (xSemaphoreTake(wifi_context.request_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return false;
    }
    while ((uint32_t)((xTaskGetTickCount() - started) * portTICK_PERIOD_MS) <
           timeout_ms) {
        if (!wifi_esp_at_rx_pop(&byte)) {
            vTaskDelay(pdMS_TO_TICKS(WIFI_ESP_AT_RX_POLL_MS));
            continue;
        }
        if (payload_length > 0U) {
            data[received++] = byte;
            if (received == payload_length) {
                *size = received;
                (void)xSemaphoreGive(wifi_context.request_mutex);
                return true;
            }
            continue;
        }
        if (header_length + 1U < sizeof(header)) {
            header[header_length++] = (char)byte;
            header[header_length] = '\0';
        }
        if (strstr(header, "+IPD,") != NULL) {
            char *colon = strchr(header, ':');

            if (colon != NULL) {
                payload_length = strtoul(strstr(header, "+IPD,") + 5, NULL, 10);
                if ((payload_length == 0U) || (payload_length >= capacity)) {
                    wifi_context.tcp_connected = false;
                    break;
                }
                header_length = 0U;
            }
        }
        if (wifi_esp_at_response_is_link_error(header) ||
            (strstr(header, "WIFI DISCONNECT") != NULL)) {
            wifi_context.tcp_connected = false;
            wifi_context.state = WIFI_STATE_ERROR;
            break;
        }
    }
    (void)xSemaphoreGive(wifi_context.request_mutex);
    return false;
}

bool wifi_esp_at_probe(char *response, size_t response_capacity) {
    bool result;

    if ((response == NULL) || (response_capacity < 2U)) {
        return false;
    }
    if (!wifi_context.is_ready || (wifi_context.request_mutex == NULL) ||
        (xSemaphoreTake(
            wifi_context.request_mutex,
            pdMS_TO_TICKS(WIFI_ESP_AT_AT_TIMEOUT_MS)) != pdTRUE)) {
        return false;
    }
    result = wifi_esp_at_command(
        "AT\r\n", "OK", WIFI_ESP_AT_AT_TIMEOUT_MS, response, response_capacity);
    (void)xSemaphoreGive(wifi_context.request_mutex);
    return result;
}

static bool wifi_esp_at_is_ready(void) {
    return wifi_context.is_ready;
}

static wifi_state_t wifi_esp_at_get_state(void) {
    return wifi_context.state;
}

static void wifi_esp_at_disconnect(void) {
    char response[WIFI_ESP_AT_RESPONSE_SIZE] = {0};

    wifi_esp_at_socket_close();
    if (wifi_context.is_ready && (wifi_context.request_mutex != NULL) &&
        (xSemaphoreTake(
             wifi_context.request_mutex,
             pdMS_TO_TICKS(WIFI_ESP_AT_AT_TIMEOUT_MS)) == pdTRUE)) {
        (void)wifi_esp_at_command(
            "AT+CWQAP\r\n", "OK", WIFI_ESP_AT_AT_TIMEOUT_MS,
            response, sizeof(response));
        wifi_context.state = WIFI_STATE_READY;
        (void)xSemaphoreGive(wifi_context.request_mutex);
    }
}

static wifi_backend_t wifi_esp_at_backend = {
    .name = "esp_at",
    .capabilities = {
        .tcp = true,
        .udp = false,
        .tls = false,
    },
    .is_ready = wifi_esp_at_is_ready,
    .get_state = wifi_esp_at_get_state,
    .connect = wifi_esp_at_connect,
    .disconnect = wifi_esp_at_disconnect,
    .socket_open = wifi_esp_at_socket_open,
    .socket_send = wifi_esp_at_socket_send,
    .socket_receive = wifi_esp_at_socket_receive,
    .socket_close = wifi_esp_at_socket_close,
    .socket_is_open = wifi_esp_at_socket_is_open,
};

static ark_component_result_t wifi_esp_at_init(void) {
    /*
     * ESP-01 ROM output can use a different baud rate immediately after reset.
     * Its boot banner is long enough to overrun 115200 reception on some
     * modules, so arm USART3 only after the full boot interval has elapsed.
     */
    if (!wifi_esp_at_load_config()) {
        return ARK_COMPONENT_ERROR;
    }
    vTaskDelay(pdMS_TO_TICKS(WIFI_ESP_AT_BOOT_SETTLE_MS));

    wifi_context.request_mutex = xSemaphoreCreateMutexStatic(
        &wifi_context.request_mutex_control);
    if ((wifi_context.request_mutex == NULL) ||
        (xTaskCreate(wifi_esp_at_rx_task, "espAtRx", WIFI_ESP_AT_RX_TASK_STACK_DEPTH, NULL,
                     tskIDLE_PRIORITY + 1U, &wifi_context.rx_task) != pdPASS)) {
        wifi_context.state = WIFI_STATE_ERROR;
        return ARK_COMPONENT_ERROR;
    }

    wifi_context.is_ready = true;
    wifi_context.state = WIFI_STATE_READY;
    wifi_esp_at_backend.capabilities.udp = wifi_config.udp_enabled;
    wifi_esp_at_backend.capabilities.tls = wifi_config.tls_enabled;
    return wifi_backend_attach(&wifi_esp_at_backend) ?
           ARK_COMPONENT_OK : ARK_COMPONENT_ERROR;
}

#if ARK_DTS_HAS_UART_CLI
static void wifi_esp_at_cli_print_response(const char *response, size_t capacity) {
    size_t response_length = wifi_esp_at_response_length(response, capacity);
    size_t index;

    printf("wifi rx[%lu]:", (unsigned long)response_length);
    for (index = 0U; index < response_length; ++index) {
        printf(" %02X", (unsigned int)(uint8_t)response[index]);
    }
    printf("\r\n");
}

static int wifi_esp_at_cli(int argc, char *argv[]) {
    if ((argc == 2) && (strcmp(argv[1], "status") == 0)) {
        printf("wifi state=%u ready=%u\r\n", (unsigned int)wifi_esp_at_get_state(),
               wifi_esp_at_is_ready() ? 1U : 0U);
        return 0;
    }
    if ((argc == 2) && (strcmp(argv[1], "connect") == 0)) {
        char diagnostic[WIFI_ESP_AT_RESPONSE_SIZE] = {0};
        bool connected = wifi_esp_at_connect_internal(
            diagnostic, sizeof(diagnostic));

        printf("wifi %s\r\n", connected ? "connected" : "failed");
        if (!connected && (diagnostic[0] != '\0')) {
            printf("wifi connect detail: %s\r\n", diagnostic);
        }
        return connected ? 0 : -1;
    }
    if ((argc == 2) && (strcmp(argv[1], "at") == 0)) {
        char response[WIFI_ESP_AT_RESPONSE_SIZE] = {0};
        bool is_ok;

        is_ok = wifi_esp_at_probe(response, sizeof(response));
        wifi_esp_at_cli_print_response(response, sizeof(response));
        if (is_ok) {
            printf("wifi at: OK\r\n");
            return 0;
        }
        printf("wifi at: no OK (bytes=%lu errors=%lu)\r\n",
               (unsigned long)wifi_context.rx_byte_count,
               (unsigned long)wifi_context.rx_error_count);
        return -1;
    }
    if ((argc == 2) && (strcmp(argv[1], "diag") == 0)) {
        printf(
            "wifi uart=%u baud=%lu ready=%u state=%u bytes=%lu errors=%lu\r\n",
               (unsigned int)wifi_config.uart_id,
               (unsigned long)wifi_config.baud_rate,
               wifi_context.is_ready ? 1U : 0U,
               (unsigned int)wifi_context.state,
               (unsigned long)wifi_context.rx_byte_count,
               (unsigned long)wifi_context.rx_error_count);
        if (wifi_context.last_connect_failure[0] != '\0') {
            printf("wifi last failure: %s\r\n",
                   wifi_context.last_connect_failure);
        }
        return 0;
    }
    printf("usage: wifi <status|at|connect|diag>\r\n");
    return -1;
}

static ark_cli_command_t wifi_esp_at_cli_command = {
    .name = "wifi",
    .usage = "wifi <status|at|connect|diag>",
    .description = "show, probe, or connect ESP AT Wi-Fi",
    .handler = wifi_esp_at_cli,
};
#endif

static const ark_component_t wifi_esp_at_component = {
    .name = "wifi_esp_at",
    .init = wifi_esp_at_init,
    .self_test = NULL,
    .self_test_expected_ms = 0U,
    .init_level = ARK_COMPONENT_INIT_LEVEL_2,
};

bool wifi_esp_at_register(void) {
#if ARK_DTS_HAS_UART_CLI
    if (!ark_cli_register(&wifi_esp_at_cli_command)) {
        return false;
    }
#endif
    return ark_component_register(&wifi_esp_at_component);
}
