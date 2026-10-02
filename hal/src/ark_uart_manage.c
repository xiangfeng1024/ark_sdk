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

#include "ark_uart_manage.h"

#include <string.h>

#define ARK_UART_IRQ_LISTENER_COUNT 4U

typedef struct {
    ark_uart_irq_callback_t callback;
    void *context;
} ark_uart_irq_listener_t;

typedef struct {
    ark_uart_t *uart;
    size_t rx_position;
    bool is_receiving;
} ark_uart_slot_t;

static ark_uart_slot_t ark_uart_slots[ARK_HAL_UART_COUNT];
static ark_uart_irq_listener_t
    ark_uart_listeners[ARK_HAL_UART_COUNT][ARK_UART_IRQ_LISTENER_COUNT];

static void ark_uart_manage_dispatch(const ark_uart_irq_info_t *info) {
    ark_uart_t *uart = ark_uart_slots[info->uart_id].uart;
    size_t index;

    if ((uart != NULL) && (uart->irq_callback != NULL)) {
        uart->irq_callback(info, uart->irq_context);
    }

    for (index = 0U; index < ARK_UART_IRQ_LISTENER_COUNT; ++index) {
        if (ark_uart_listeners[info->uart_id][index].callback != NULL) {
            ark_uart_listeners[info->uart_id][index].callback(
                info,
                ark_uart_listeners[info->uart_id][index].context);
        }
    }
}

static void ark_uart_manage_dispatch_data(
    ark_uart_t *uart,
    ark_uart_irq_event_t event,
    size_t offset,
    size_t size) {
    ark_uart_irq_info_t info;

    if (size == 0U) {
        return;
    }

    info.uart_id = uart->uart_id;
    info.event = event;
    info.data = &uart->rx_buffer[offset];
    info.size = size;
    info.error_code = 0U;
    ark_uart_manage_dispatch(&info);
}

static bool ark_uart_manage_restart_receive(ark_uart_slot_t *slot) {
    ark_uart_t *uart = slot->uart;

    slot->rx_position = 0U;
    if (uart->rx_mode == ARK_UART_RX_INTERRUPT) {
        return ark_hal_uart.receive_it(
            uart->uart_id,
            uart->rx_buffer,
            uart->rx_buffer_size);
    }
    if (uart->rx_mode == ARK_UART_RX_DMA) {
        return ark_hal_uart.receive_dma(
            uart->uart_id,
            uart->rx_buffer,
            uart->rx_buffer_size);
    }
    if (uart->rx_mode == ARK_UART_RX_DMA_TO_IDLE) {
        return ark_hal_uart.receive_to_idle_dma(
            uart->uart_id,
            uart->rx_buffer,
            uart->rx_buffer_size);
    }
    return false;
}

bool ark_uart_manage_register(ark_uart_t *uart) {
    size_t index;

    if ((uart == NULL) || (uart->name == NULL) ||
        (uart->uart_id >= ARK_HAL_UART_COUNT) ||
        (uart->tx_mode > ARK_UART_TX_DMA) ||
        (uart->rx_mode > ARK_UART_RX_DMA_TO_IDLE) ||
        !ark_hal_uart.is_ready(uart->uart_id)) {
        return false;
    }
    if ((uart->rx_mode != ARK_UART_RX_DISABLED) &&
        ((uart->rx_buffer == NULL) || (uart->rx_buffer_size == 0U) ||
         (uart->rx_buffer_size > UINT16_MAX))) {
        return false;
    }
    if (ark_uart_slots[uart->uart_id].uart != NULL) {
        return false;
    }
    for (index = 0U; index < ARK_HAL_UART_COUNT; ++index) {
        if ((ark_uart_slots[index].uart != NULL) &&
            (strcmp(ark_uart_slots[index].uart->name, uart->name) == 0)) {
            return false;
        }
    }

    ark_uart_slots[uart->uart_id].uart = uart;
    ark_uart_slots[uart->uart_id].rx_position = 0U;
    ark_uart_slots[uart->uart_id].is_receiving = false;
    return true;
}

bool ark_uart_manage_unregister(ark_uart_t *uart) {
    ark_uart_slot_t *slot;

    if ((uart == NULL) || (uart->uart_id >= ARK_HAL_UART_COUNT)) {
        return false;
    }
    slot = &ark_uart_slots[uart->uart_id];
    if (slot->uart != uart) {
        return false;
    }
    if (slot->is_receiving) {
        ark_hal_uart.abort(uart->uart_id);
    }
    slot->uart = NULL;
    slot->rx_position = 0U;
    slot->is_receiving = false;
    return true;
}

const ark_uart_t *ark_uart_manage_get(ark_hal_uart_id_t uart_id) {
    if (uart_id >= ARK_HAL_UART_COUNT) {
        return NULL;
    }
    return ark_uart_slots[uart_id].uart;
}

bool ark_uart_manage_start_receive(ark_uart_t *uart) {
    ark_uart_slot_t *slot;

    if ((uart == NULL) || (uart->uart_id >= ARK_HAL_UART_COUNT) ||
        (uart->rx_mode == ARK_UART_RX_DISABLED)) {
        return false;
    }
    slot = &ark_uart_slots[uart->uart_id];
    if ((slot->uart != uart) || slot->is_receiving) {
        return false;
    }

    slot->is_receiving = ark_uart_manage_restart_receive(slot);
    return slot->is_receiving;
}

int32_t ark_uart_manage_transmit(
    ark_uart_t *uart,
    const uint8_t *data,
    size_t size,
    uint32_t timeout_ms) {
    if ((uart == NULL) || (uart->uart_id >= ARK_HAL_UART_COUNT) ||
        (ark_uart_slots[uart->uart_id].uart != uart)) {
        return -1;
    }
    if (uart->tx_mode == ARK_UART_TX_DMA) {
        return ark_hal_uart.transmit_dma(uart->uart_id, data, size) ? (int32_t)size : -1;
    }
    return ark_hal_uart.transmit(uart->uart_id, data, size, timeout_ms);
}

bool ark_uart_manage_register_irq(
    ark_hal_uart_id_t uart_id,
    ark_uart_irq_callback_t callback,
    void *context) {
    size_t index;

    if ((uart_id >= ARK_HAL_UART_COUNT) || (callback == NULL)) {
        return false;
    }
    for (index = 0U; index < ARK_UART_IRQ_LISTENER_COUNT; ++index) {
        if (ark_uart_listeners[uart_id][index].callback == NULL) {
            ark_uart_listeners[uart_id][index].callback = callback;
            ark_uart_listeners[uart_id][index].context = context;
            return true;
        }
        if ((ark_uart_listeners[uart_id][index].callback == callback) &&
            (ark_uart_listeners[uart_id][index].context == context)) {
            return false;
        }
    }
    return false;
}

bool ark_uart_manage_unregister_irq(
    ark_hal_uart_id_t uart_id,
    ark_uart_irq_callback_t callback,
    void *context) {
    size_t index;

    if ((uart_id >= ARK_HAL_UART_COUNT) || (callback == NULL)) {
        return false;
    }
    for (index = 0U; index < ARK_UART_IRQ_LISTENER_COUNT; ++index) {
        if ((ark_uart_listeners[uart_id][index].callback == callback) &&
            (ark_uart_listeners[uart_id][index].context == context)) {
            ark_uart_listeners[uart_id][index].callback = NULL;
            ark_uart_listeners[uart_id][index].context = NULL;
            return true;
        }
    }
    return false;
}

void ark_uart_manage_handle_irq(
    ark_hal_uart_id_t uart_id,
    ark_uart_irq_event_t event,
    uint32_t error_code) {
    ark_uart_irq_info_t info;
    ark_uart_slot_t *slot;

    if (uart_id >= ARK_HAL_UART_COUNT) {
        return;
    }

    info.uart_id = uart_id;
    info.event = event;
    info.data = NULL;
    info.size = 0U;
    info.error_code = error_code;
    ark_uart_manage_dispatch(&info);

    slot = &ark_uart_slots[uart_id];
    if ((event == ARK_UART_IRQ_ERROR) &&
        (slot->uart != NULL) &&
        slot->is_receiving) {
        ark_hal_uart.abort(uart_id);
        slot->is_receiving = ark_uart_manage_restart_receive(slot);
    }
}

void ark_uart_manage_handle_rx_event(
    ark_hal_uart_id_t uart_id,
    ark_uart_irq_event_t event,
    size_t position) {
    ark_uart_slot_t *slot;
    ark_uart_t *uart;
    bool is_circular;
    size_t next_position;

    if (uart_id >= ARK_HAL_UART_COUNT) {
        return;
    }
    slot = &ark_uart_slots[uart_id];
    uart = slot->uart;
    if ((uart == NULL) || !slot->is_receiving ||
        (position == 0U) || (position > uart->rx_buffer_size)) {
        return;
    }

    is_circular = ark_hal_uart.rx_dma_is_circular(uart_id);
    next_position = (position == uart->rx_buffer_size) ? 0U : position;

    if (position > slot->rx_position) {
        ark_uart_manage_dispatch_data(
            uart,
            event,
            slot->rx_position,
            position - slot->rx_position);
    } else if (is_circular && (position < slot->rx_position)) {
        ark_uart_manage_dispatch_data(
            uart,
            event,
            slot->rx_position,
            uart->rx_buffer_size - slot->rx_position);
        ark_uart_manage_dispatch_data(uart, event, 0U, position);
    }
    slot->rx_position = next_position;

    if (!is_circular &&
        ((event == ARK_UART_IRQ_RX_COMPLETE) || (event == ARK_UART_IRQ_RX_IDLE))) {
        slot->is_receiving = ark_uart_manage_restart_receive(slot);
        if (!slot->is_receiving) {
            ark_uart_manage_handle_irq(uart_id, ARK_UART_IRQ_ERROR, 0U);
        }
    }
}
