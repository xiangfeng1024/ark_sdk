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

#include "uart_cli.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "ark_cli.h"
#include "ark_component.h"
#include "ark_dts.h"
#include "ark_stream.h"

#define UART_CLI_LINE_SIZE 96U
#define UART_CLI_TASK_STACK_DEPTH 384U
#define UART_CLI_WRITE_TIMEOUT_MS 1000U
#define UART_CLI_WELCOME_RETRY_DELAY_MS 100U
#define UART_CLI_SELF_TEST_EXPECTED_MS 250U

static TaskHandle_t uart_cli_task_handle;
static ark_stream_t *uart_cli_stream;
static bool is_uart_cli_ready;
static uart_cli_config_t uart_cli_config;

const uart_cli_config_t *uart_cli_config_get(void) {
    return &uart_cli_config;
}

static bool uart_cli_load_config(void) {
    const ark_of_node_t *node =
        ark_of_find_compatible_node(NULL, "uart_cli");
    const char *stream_name;
    const char *welcome;

    if ((node == NULL) ||
        (ark_of_property_read_string(node, "ark,stream-name", &stream_name) != 0) ||
        (ark_of_property_read_string(node, "ark,welcome", &welcome) != 0)) {
        return false;
    }
    uart_cli_config.stream_name = stream_name;
    uart_cli_config.welcome = (const uint8_t *)welcome;
    uart_cli_config.welcome_size = strlen(welcome);
    return uart_cli_config.welcome_size > 0U;
}

static void uart_cli_print_prompt(void) {
    static const uint8_t prompt[] = "xy> ";

    (void)uart_cli_write(prompt, sizeof(prompt) - 1U);
}

static void uart_cli_print_newline(void) {
    static const uint8_t newline[] = "\r\n";

    (void)uart_cli_write(newline, sizeof(newline) - 1U);
}

static void uart_cli_move_cursor_left(size_t count) {
    char sequence[16];
    int length;

    if (count == 0U) {
        return;
    }
    length = snprintf(
        sequence, sizeof(sequence), "\x1b[%luD", (unsigned long)count);
    if (length > 0) {
        (void)uart_cli_write((const uint8_t *)sequence, (size_t)length);
    }
}

static void uart_cli_redraw_line(const char *line, size_t length) {
    static const uint8_t clear_line[] = "\r\x1b[2K";

    (void)uart_cli_write(clear_line, sizeof(clear_line) - 1U);
    uart_cli_print_prompt();
    if (length > 0U) {
        (void)uart_cli_write((const uint8_t *)line, length);
    }
}

static int uart_cli_help(int argc, char *argv[]) {
    if (argc != 1) {
        printf("usage: %s\r\n", argv[0]);
        return -1;
    }
    ark_cli_print_help();
    return 0;
}

static int uart_cli_hello(int argc, char *argv[]) {
    if (argc != 1) {
        printf("usage: %s\r\n", argv[0]);
        return -1;
    }
    printf("hello, ark!\r\n");
    return 0;
}

static ark_cli_command_t uart_cli_help_command = {
    .name = "help",
    .usage = "help",
    .description = "show registered commands",
    .handler = uart_cli_help,
};

static ark_cli_command_t uart_cli_hello_command = {
    .name = "hello",
    .usage = "hello",
    .description = "print greeting",
    .handler = uart_cli_hello,
};

static void uart_cli_task(void *argument) {
    char line[UART_CLI_LINE_SIZE];
    char history[UART_CLI_LINE_SIZE] = {0};
    char draft[UART_CLI_LINE_SIZE] = {0};
    size_t length = 0U;
    size_t cursor = 0U;
    size_t draft_length = 0U;
    uint8_t character;
    bool last_was_carriage_return = false;
    bool history_active = false;
    bool escape_pending = false;
    bool arrow_pending = false;

    (void)argument;
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    while (uart_cli_write(
               ark_uart_cli_config.welcome,
               ark_uart_cli_config.welcome_size) !=
           (int32_t)ark_uart_cli_config.welcome_size) {
        vTaskDelay(pdMS_TO_TICKS(UART_CLI_WELCOME_RETRY_DELAY_MS));
    }
    uart_cli_print_newline();
    uart_cli_print_prompt();
    for (;;) {
        if (ark_stream_read(
                uart_cli_stream,
                &character,
                1U,
                ARK_STREAM_WAIT_FOREVER) != 1) {
            continue;
        }
        if (escape_pending) {
            escape_pending = false;
            arrow_pending = (character == '[') || (character == 'O');
            continue;
        }
        if (arrow_pending) {
            arrow_pending = false;
            if ((character == 'A') && (history[0] != '\0')) {
                if (!history_active) {
                    memcpy(draft, line, length);
                    draft[length] = '\0';
                    draft_length = length;
                }
                strcpy(line, history);
                length = strlen(line);
                cursor = length;
                history_active = true;
                uart_cli_redraw_line(line, length);
            } else if ((character == 'B') && history_active) {
                memcpy(line, draft, draft_length);
                line[draft_length] = '\0';
                length = draft_length;
                cursor = length;
                history_active = false;
                uart_cli_redraw_line(line, length);
            } else if ((character == 'C') && (cursor < length)) {
                static const uint8_t cursor_right[] = "\x1b[C";

                (void)uart_cli_write(
                    cursor_right, sizeof(cursor_right) - 1U);
                ++cursor;
            } else if ((character == 'D') && (cursor > 0U)) {
                static const uint8_t cursor_left[] = "\x1b[D";

                (void)uart_cli_write(cursor_left, sizeof(cursor_left) - 1U);
                --cursor;
            }
            continue;
        }
        if (character == 0x1BU) {
            escape_pending = true;
            continue;
        }
        if ((character == '\n') && last_was_carriage_return) {
            last_was_carriage_return = false;
            continue;
        }
        if ((character == '\r') || (character == '\n')) {
            static const uint8_t newline[] = "\r\n";

            last_was_carriage_return = character == '\r';
            (void)uart_cli_write(newline, sizeof(newline) - 1U);
            if (length > 0U) {
                line[length] = '\0';
                strcpy(history, line);
                ark_cli_execute(line);
                length = 0U;
                cursor = 0U;
                history_active = false;
            }
            uart_cli_print_prompt();
        } else if ((character == '\b') || (character == 0x7FU)) {
            last_was_carriage_return = false;
            if (cursor > 0U) {
                static const uint8_t backspace[] = "\b";
                static const uint8_t blank[] = " ";

                --cursor;
                memmove(&line[cursor], &line[cursor + 1U], length - cursor - 1U);
                --length;
                history_active = false;
                (void)uart_cli_write(backspace, sizeof(backspace) - 1U);
                if (length > cursor) {
                    (void)uart_cli_write(
                        (const uint8_t *)&line[cursor], length - cursor);
                }
                (void)uart_cli_write(blank, sizeof(blank) - 1U);
                uart_cli_move_cursor_left(length - cursor + 1U);
            }
        } else if ((character >= 0x20U) && (character <= 0x7EU) &&
                   (length < (UART_CLI_LINE_SIZE - 1U))) {
            last_was_carriage_return = false;
            memmove(&line[cursor + 1U], &line[cursor], length - cursor);
            line[cursor] = (char)character;
            ++length;
            ++cursor;
            history_active = false;
            (void)uart_cli_write(
                (const uint8_t *)&line[cursor - 1U], length - cursor + 1U);
            uart_cli_move_cursor_left(length - cursor);
        }
    }
}

static ark_component_result_t uart_cli_init(void) {
    if ((ark_uart_cli_config.stream_name == NULL) ||
        (ark_uart_cli_config.welcome == NULL) ||
        (ark_uart_cli_config.welcome_size == 0U)) {
        return ARK_COMPONENT_ERROR;
    }
    uart_cli_stream = ark_stream_find(ark_uart_cli_config.stream_name);
    if (!ark_stream_is_ready(uart_cli_stream)) {
        return ARK_COMPONENT_ERROR;
    }

    if (xTaskCreate(
            uart_cli_task,
            "uartCli",
            UART_CLI_TASK_STACK_DEPTH,
            NULL,
            tskIDLE_PRIORITY + 1U,
            &uart_cli_task_handle) != pdPASS) {
        return ARK_COMPONENT_ERROR;
    }

    is_uart_cli_ready = true;
    return ARK_COMPONENT_OK;
}

static ark_component_result_t uart_cli_self_test(void) {
    return uart_cli_start() ? ARK_COMPONENT_OK : ARK_COMPONENT_ERROR;
}

static ark_component_t uart_cli_component = {
    .name = "uart_cli",
    .init = uart_cli_init,
    .self_test = uart_cli_self_test,
    .self_test_expected_ms = UART_CLI_SELF_TEST_EXPECTED_MS,
    .init_level = ARK_COMPONENT_INIT_LEVEL_2,
};

bool uart_cli_register(void) {
    bool commands_ok;

    if (!uart_cli_load_config()) {
        return false;
    }
    commands_ok = ark_cli_register(&uart_cli_help_command);
    commands_ok = ark_cli_register(&uart_cli_hello_command) && commands_ok;
    return ark_component_register(&uart_cli_component) && commands_ok;
}

bool uart_cli_start(void) {
    if (!is_uart_cli_ready || (uart_cli_task_handle == NULL)) {
        return false;
    }
    (void)xTaskNotifyGive(uart_cli_task_handle);
    return true;
}

int32_t uart_cli_write(const uint8_t *data, size_t size) {
    if (!is_uart_cli_ready || (data == NULL) || (size == 0U) ||
        (size > UINT16_MAX)) {
        return -1;
    }
    return ark_stream_write(
        uart_cli_stream,
        data,
        size,
        UART_CLI_WRITE_TIMEOUT_MS);
}

int fputc(int character, FILE *stream) {
    uint8_t data = (uint8_t)character;

    (void)stream;
    (void)uart_cli_write(&data, 1U);
    return character;
}
