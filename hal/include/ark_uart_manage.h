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

#ifndef ARK_UART_MANAGE_H
#define ARK_UART_MANAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ark_hal_uart.h"

typedef enum {
    ARK_UART_TX_POLLING = 0,
    ARK_UART_TX_DMA
} ark_uart_tx_mode_t;

typedef enum {
    ARK_UART_RX_DISABLED = 0,
    ARK_UART_RX_INTERRUPT,
    ARK_UART_RX_DMA,
    ARK_UART_RX_DMA_TO_IDLE
} ark_uart_rx_mode_t;

typedef enum {
    ARK_UART_IRQ_TX_COMPLETE = 0,
    ARK_UART_IRQ_RX_HALF,
    ARK_UART_IRQ_RX_COMPLETE,
    ARK_UART_IRQ_RX_IDLE,
    ARK_UART_IRQ_ERROR
} ark_uart_irq_event_t;

typedef struct {
    ark_hal_uart_id_t uart_id;
    ark_uart_irq_event_t event;
    uint8_t *data;
    size_t size;
    uint32_t error_code;
} ark_uart_irq_info_t;

typedef void (*ark_uart_irq_callback_t)(const ark_uart_irq_info_t *info, void *context);

typedef struct {
    const char *name;
    ark_hal_uart_id_t uart_id;
    ark_uart_tx_mode_t tx_mode;
    ark_uart_rx_mode_t rx_mode;
    uint8_t *rx_buffer;
    size_t rx_buffer_size;
    ark_uart_irq_callback_t irq_callback;
    void *irq_context;
} ark_uart_t;

bool ark_uart_manage_register(ark_uart_t *uart);
bool ark_uart_manage_unregister(ark_uart_t *uart);
const ark_uart_t *ark_uart_manage_get(ark_hal_uart_id_t uart_id);
bool ark_uart_manage_start_receive(ark_uart_t *uart);
int32_t ark_uart_manage_transmit(
    ark_uart_t *uart,
    const uint8_t *data,
    size_t size,
    uint32_t timeout_ms);
bool ark_uart_manage_register_irq(
    ark_hal_uart_id_t uart_id,
    ark_uart_irq_callback_t callback,
    void *context);
bool ark_uart_manage_unregister_irq(
    ark_hal_uart_id_t uart_id,
    ark_uart_irq_callback_t callback,
    void *context);
void ark_uart_manage_handle_irq(
    ark_hal_uart_id_t uart_id,
    ark_uart_irq_event_t event,
    uint32_t error_code);
void ark_uart_manage_handle_rx_event(
    ark_hal_uart_id_t uart_id,
    ark_uart_irq_event_t event,
    size_t position);

#endif /* ARK_UART_MANAGE_H */
