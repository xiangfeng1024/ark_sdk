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

#include "stream.h"

#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "stream_buffer.h"
#include "ark_component.h"
#include "ark_dts.h"
#include "ark_stream.h"
#include "ark_uart_manage.h"

#define ARK_STREAM_UART_MAX_COUNT ARK_HAL_UART_COUNT
#define ARK_STREAM_UART_DMA_RX_SIZE 64U
#define ARK_STREAM_UART_RX_SIZE 256U
#define ARK_STREAM_UART_INTERRUPT_RING_SIZE 128U

typedef enum {
    ARK_STREAM_RX_DMA_TO_IDLE = 0,
    ARK_STREAM_RX_INTERRUPT,
    ARK_STREAM_RX_DMA
} ark_stream_rx_mode_t;

typedef struct {
    ark_uart_t uart;
    uint8_t dma_rx[ARK_STREAM_UART_DMA_RX_SIZE];
    uint8_t rx_storage[ARK_STREAM_UART_RX_SIZE];
    StaticStreamBuffer_t rx_control;
    StreamBufferHandle_t rx;
    StaticSemaphore_t tx_mutex_control;
    StaticSemaphore_t tx_done_control;
    SemaphoreHandle_t tx_mutex;
    SemaphoreHandle_t tx_done;
    volatile bool tx_failed;
    bool ready;
    ark_stream_rx_mode_t rx_mode;
    uint8_t interrupt_rx;
    uint8_t interrupt_ring[ARK_STREAM_UART_INTERRUPT_RING_SIZE];
    volatile uint16_t interrupt_read;
    volatile uint16_t interrupt_write;
} ark_stream_uart_context_t;

static void ark_stream_uart_irq(const ark_uart_irq_info_t *info, void *context);
static bool ark_stream_uart_is_ready(void *context);
static int32_t ark_stream_uart_read(
    void *context,
    uint8_t *data,
    size_t size,
    uint32_t timeout_ms);
static int32_t ark_stream_uart_write(
    void *context,
    const uint8_t *data,
    size_t size,
    uint32_t timeout_ms);

static const ark_stream_operations_t ark_stream_uart_operations = {
    .is_ready = ark_stream_uart_is_ready,
    .read = ark_stream_uart_read,
    .write = ark_stream_uart_write,
};
static ark_stream_uart_context_t ark_stream_uart_contexts[ARK_STREAM_UART_MAX_COUNT];
static ark_stream_t ark_streams[ARK_STREAM_UART_MAX_COUNT];

static TickType_t ark_stream_timeout_ticks(uint32_t timeout_ms) {
    if (timeout_ms == ARK_STREAM_WAIT_FOREVER) {
        return portMAX_DELAY;
    }
    return pdMS_TO_TICKS(timeout_ms);
}

static void ark_stream_uart_irq(const ark_uart_irq_info_t *info, void *context) {
    ark_stream_uart_context_t *uart_context = context;
    BaseType_t higher_priority_task_woken = pdFALSE;

    if (info->event == ARK_UART_IRQ_TX_COMPLETE) {
        uart_context->tx_failed = false;
        xSemaphoreGiveFromISR(uart_context->tx_done, &higher_priority_task_woken);
    } else if ((info->event == ARK_UART_IRQ_RX_HALF) ||
               (info->event == ARK_UART_IRQ_RX_IDLE) ||
               (info->event == ARK_UART_IRQ_RX_COMPLETE)) {
        if (uart_context->rx_mode == ARK_STREAM_RX_INTERRUPT) {
            size_t index;

            for (index = 0U; index < info->size; ++index) {
                uint16_t next = (uint16_t)(
                    (uart_context->interrupt_write + 1U) %
                    ARK_STREAM_UART_INTERRUPT_RING_SIZE);

                if (next == uart_context->interrupt_read) {
                    continue;
                }
                uart_context->interrupt_ring[uart_context->interrupt_write] =
                    info->data[index];
                uart_context->interrupt_write = next;
            }
        } else {
            xStreamBufferSendFromISR(
                uart_context->rx,
                info->data,
                info->size,
                &higher_priority_task_woken);
        }
    } else if (info->event == ARK_UART_IRQ_ERROR) {
        if (uart_context->rx_mode != ARK_STREAM_RX_INTERRUPT) {
            uart_context->tx_failed = true;
            xSemaphoreGiveFromISR(uart_context->tx_done, &higher_priority_task_woken);
        }
    }
    if (uart_context->rx_mode != ARK_STREAM_RX_INTERRUPT) {
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

static bool ark_stream_parse_rx_mode(
    const ark_of_node_t *node,
    ark_stream_rx_mode_t *mode) {
    const char *value;

    if (mode == NULL) {
        return false;
    }
    if (ark_of_property_read_string(node, "ark,rx-mode", &value) != 0) {
        *mode = ARK_STREAM_RX_DMA_TO_IDLE;
        return true;
    }
    if (strcmp(value, "interrupt") == 0) {
        *mode = ARK_STREAM_RX_INTERRUPT;
        return true;
    }
    if (strcmp(value, "dma") == 0) {
        *mode = ARK_STREAM_RX_DMA;
        return true;
    }
    if (strcmp(value, "dma-to-idle") == 0) {
        *mode = ARK_STREAM_RX_DMA_TO_IDLE;
        return true;
    }
    return false;
}

static bool ark_stream_uart_is_ready(void *context) {
    return ((ark_stream_uart_context_t *)context)->ready;
}

static int32_t ark_stream_uart_read(
    void *context,
    uint8_t *data,
    size_t size,
    uint32_t timeout_ms) {
    ark_stream_uart_context_t *uart_context = context;

    if (!uart_context->ready || (data == NULL) || (size == 0U)) {
        return -1;
    }
    if (uart_context->rx_mode == ARK_STREAM_RX_INTERRUPT) {
        TickType_t started = xTaskGetTickCount();
        size_t count = 0U;

        while (count < size) {
            if (uart_context->interrupt_read != uart_context->interrupt_write) {
                data[count++] = uart_context->interrupt_ring[
                    uart_context->interrupt_read];
                uart_context->interrupt_read = (uint16_t)(
                    (uart_context->interrupt_read + 1U) %
                    ARK_STREAM_UART_INTERRUPT_RING_SIZE);
                continue;
            }
            if ((timeout_ms != ARK_STREAM_WAIT_FOREVER) &&
                ((xTaskGetTickCount() - started) >= ark_stream_timeout_ticks(timeout_ms))) {
                break;
            }
            vTaskDelay(1U);
        }
        return (int32_t)count;
    }
    return (int32_t)xStreamBufferReceive(
        uart_context->rx,
        data,
        size,
        ark_stream_timeout_ticks(timeout_ms));
}

static int32_t ark_stream_uart_write(
    void *context,
    const uint8_t *data,
    size_t size,
    uint32_t timeout_ms) {
    ark_stream_uart_context_t *uart_context = context;
    TickType_t timeout = ark_stream_timeout_ticks(timeout_ms);
    int32_t result = -1;

    if (!uart_context->ready || (size > 65535U)) {
        return -1;
    }
    if (xSemaphoreTake(uart_context->tx_mutex, timeout) != pdTRUE) {
        return -1;
    }
    if (uart_context->uart.tx_mode == ARK_UART_TX_POLLING) {
        result = ark_uart_manage_transmit(&uart_context->uart, data, size, timeout_ms);
    } else {
        (void)xSemaphoreTake(uart_context->tx_done, 0U);
        uart_context->tx_failed = false;
        if ((ark_uart_manage_transmit(&uart_context->uart, data, size, 0U) == (int32_t)size) &&
            (xSemaphoreTake(uart_context->tx_done, timeout) == pdTRUE) &&
            !uart_context->tx_failed) {
            result = (int32_t)size;
        }
    }
    (void)xSemaphoreGive(uart_context->tx_mutex);
    return result;
}

static ark_component_result_t stream_init(void) {
    const ark_of_node_t *node = NULL;
    size_t count = 0U;

    memset(ark_stream_uart_contexts, 0, sizeof(ark_stream_uart_contexts));
    memset(ark_streams, 0, sizeof(ark_streams));
    while ((node = ark_of_find_compatible_node(node, "stream")) != NULL) {
        ark_stream_uart_context_t *context;
        const ark_of_node_t *parent = ark_of_get_parent(node);
        const char *name;
        uint8_t uart_id;
        uint32_t baud_rate = 0U;

        if ((count >= ARK_STREAM_UART_MAX_COUNT) ||
            (ark_of_property_read_string(node, "ark,stream-name", &name) != 0) ||
            (name[0] == '\0') ||
            (ark_of_hal_get_parent(node, ARK_OF_HAL_UART, &uart_id) != 0) ||
            (uart_id >= ARK_HAL_UART_COUNT) ||
            ((ark_of_property_read_u32(node, "current-speed", &baud_rate) != 0) &&
             ((parent == NULL) ||
              (ark_of_property_read_u32(parent, "current-speed", &baud_rate) != 0))) ||
            (baud_rate == 0U)) {
            return ARK_COMPONENT_ERROR;
        }
        context = &ark_stream_uart_contexts[count];
        context->rx_mode = ARK_STREAM_RX_DMA_TO_IDLE;
        if (!ark_stream_parse_rx_mode(node, &context->rx_mode)) {
            return ARK_COMPONENT_ERROR;
        }
        context->uart.name = name;
        context->uart.uart_id = (ark_hal_uart_id_t)uart_id;
        context->uart.tx_mode = ARK_UART_TX_POLLING;
        context->uart.rx_mode = (context->rx_mode == ARK_STREAM_RX_INTERRUPT) ?
            ARK_UART_RX_INTERRUPT :
            (context->rx_mode == ARK_STREAM_RX_DMA) ?
            ARK_UART_RX_DMA : ARK_UART_RX_DMA_TO_IDLE;
        context->uart.rx_buffer = (context->rx_mode == ARK_STREAM_RX_INTERRUPT) ?
            &context->interrupt_rx : context->dma_rx;
        context->uart.rx_buffer_size = (context->rx_mode == ARK_STREAM_RX_INTERRUPT) ?
            sizeof(context->interrupt_rx) : sizeof(context->dma_rx);
        context->uart.irq_callback = ark_stream_uart_irq;
        context->uart.irq_context = context;
        context->rx = xStreamBufferCreateStatic(
            sizeof(context->rx_storage), 1U, context->rx_storage, &context->rx_control);
        context->tx_mutex = xSemaphoreCreateMutexStatic(&context->tx_mutex_control);
        context->tx_done = xSemaphoreCreateBinaryStatic(&context->tx_done_control);
        ark_streams[count].name = name;
        ark_streams[count].operations = &ark_stream_uart_operations;
        ark_streams[count].context = context;
        if ((context->rx == NULL) || (context->tx_mutex == NULL) ||
            (context->tx_done == NULL) ||
            !ark_uart_manage_register(&context->uart) ||
            !ark_stream_attach(&ark_streams[count]) ||
            !ark_uart_manage_start_receive(&context->uart)) {
            return ARK_COMPONENT_ERROR;
        }
        context->ready = true;
        ++count;
    }
    return (count > 0U) ? ARK_COMPONENT_OK : ARK_COMPONENT_ERROR;
}

static const ark_component_t stream_component = {
    .name = "stream",
    .init = stream_init,
    .self_test = NULL,
    .self_test_expected_ms = 0U,
    .init_level = ARK_COMPONENT_INIT_LEVEL_2,
};

bool stream_register(void) {
    return ark_component_register(&stream_component);
}
