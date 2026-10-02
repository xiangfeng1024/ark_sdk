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

#include "ark_hal_uart.h"

#include "usart.h"
#include "ark_hal_bindings.h"
#include "ark_uart_manage.h"

static UART_HandleTypeDef *stm32f1_uart_handle(ark_hal_uart_id_t id) {
    return (id < ARK_HAL_UART_COUNT) ?
           (UART_HandleTypeDef *)ark_hal_uart_handles[id] : NULL;
}

static bool stm32f1_uart_resolve_id(
    UART_HandleTypeDef *handle,
    ark_hal_uart_id_t *uart_id) {
    size_t index;

    for (index = 0U; index < ARK_HAL_UART_COUNT; ++index) {
        UART_HandleTypeDef *configured_handle =
            stm32f1_uart_handle((ark_hal_uart_id_t)index);

        if ((configured_handle == handle) ||
            ((configured_handle != NULL) &&
             (configured_handle->Instance == handle->Instance))) {
            *uart_id = (ark_hal_uart_id_t)index;
            return true;
        }
    }
    return false;
}

static bool stm32f1_uart_is_ready(ark_hal_uart_id_t id) {
    UART_HandleTypeDef *handle = stm32f1_uart_handle(id);

    return (handle != NULL) && (handle->Instance != NULL) &&
           (handle->gState != HAL_UART_STATE_RESET);
}

static bool stm32f1_uart_rx_dma_is_circular(ark_hal_uart_id_t id) {
    UART_HandleTypeDef *handle = stm32f1_uart_handle(id);

    return (handle != NULL) && (handle->hdmarx != NULL) &&
           (handle->hdmarx->Init.Mode == DMA_CIRCULAR);
}

static int32_t stm32f1_uart_transmit(
    ark_hal_uart_id_t id,
    const uint8_t *data,
    size_t size,
    uint32_t timeout_ms) {
    UART_HandleTypeDef *handle = stm32f1_uart_handle(id);

    if ((handle == NULL) || (data == NULL) || (size == 0U) || (size > UINT16_MAX)) {
        return -1;
    }
    return (HAL_UART_Transmit(handle, data, (uint16_t)size, timeout_ms) == HAL_OK) ?
           (int32_t)size : -1;
}

static int32_t stm32f1_uart_receive(
    ark_hal_uart_id_t id,
    uint8_t *data,
    size_t size,
    uint32_t timeout_ms) {
    UART_HandleTypeDef *handle = stm32f1_uart_handle(id);

    if ((handle == NULL) || (data == NULL) || (size == 0U) || (size > UINT16_MAX)) {
        return -1;
    }
    return (HAL_UART_Receive(handle, data, (uint16_t)size, timeout_ms) == HAL_OK) ?
           (int32_t)size : -1;
}

static bool stm32f1_uart_transmit_dma(
    ark_hal_uart_id_t id,
    const uint8_t *data,
    size_t size) {
    UART_HandleTypeDef *handle = stm32f1_uart_handle(id);

    return (handle != NULL) && (handle->hdmatx != NULL) &&
           (data != NULL) && (size > 0U) && (size <= UINT16_MAX) &&
           (HAL_UART_Transmit_DMA(handle, data, (uint16_t)size) == HAL_OK);
}

static bool stm32f1_uart_receive_dma(
    ark_hal_uart_id_t id,
    uint8_t *data,
    size_t size) {
    UART_HandleTypeDef *handle = stm32f1_uart_handle(id);

    return (handle != NULL) && (handle->hdmarx != NULL) &&
           (data != NULL) && (size > 0U) && (size <= UINT16_MAX) &&
           (HAL_UART_Receive_DMA(handle, data, (uint16_t)size) == HAL_OK);
}

static bool stm32f1_uart_receive_it(
    ark_hal_uart_id_t id,
    uint8_t *data,
    size_t size) {
    UART_HandleTypeDef *handle = stm32f1_uart_handle(id);

    return (handle != NULL) && (data != NULL) && (size > 0U) && (size <= UINT16_MAX) &&
           (HAL_UART_Receive_IT(handle, data, (uint16_t)size) == HAL_OK);
}

static bool stm32f1_uart_receive_to_idle_dma(
    ark_hal_uart_id_t id,
    uint8_t *data,
    size_t size) {
    UART_HandleTypeDef *handle = stm32f1_uart_handle(id);

    return (handle != NULL) && (handle->hdmarx != NULL) &&
           (data != NULL) && (size > 0U) && (size <= UINT16_MAX) &&
           (HAL_UARTEx_ReceiveToIdle_DMA(handle, data, (uint16_t)size) == HAL_OK);
}

static bool stm32f1_uart_abort(ark_hal_uart_id_t id) {
    UART_HandleTypeDef *handle = stm32f1_uart_handle(id);

    return (handle != NULL) && (HAL_UART_Abort(handle) == HAL_OK);
}

const ark_hal_uart_driver_t ark_hal_uart = {
    .is_ready = stm32f1_uart_is_ready,
    .rx_dma_is_circular = stm32f1_uart_rx_dma_is_circular,
    .transmit = stm32f1_uart_transmit,
    .receive = stm32f1_uart_receive,
    .transmit_dma = stm32f1_uart_transmit_dma,
    .receive_it = stm32f1_uart_receive_it,
    .receive_dma = stm32f1_uart_receive_dma,
    .receive_to_idle_dma = stm32f1_uart_receive_to_idle_dma,
    .abort = stm32f1_uart_abort,
};

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *handle) {
    ark_hal_uart_id_t id;

    if (stm32f1_uart_resolve_id(handle, &id)) {
        ark_uart_manage_handle_irq(id, ARK_UART_IRQ_TX_COMPLETE, 0U);
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *handle) {
    ark_hal_uart_id_t id;
    size_t received_size;

    if (stm32f1_uart_resolve_id(handle, &id)) {
        /* HAL clears RxXferSize before an interrupt-mode completion callback. */
        received_size = (handle->hdmarx == NULL) ? 1U : handle->RxXferSize;
        ark_uart_manage_handle_rx_event(
            id,
            ARK_UART_IRQ_RX_COMPLETE,
            received_size);
    }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *handle, uint16_t size) {
    ark_hal_uart_id_t id;
    ark_uart_irq_event_t event;
    HAL_UART_RxEventTypeTypeDef hal_event;

    if (!stm32f1_uart_resolve_id(handle, &id)) {
        return;
    }

    hal_event = HAL_UARTEx_GetRxEventType(handle);
    event = (hal_event == HAL_UART_RXEVENT_HT) ? ARK_UART_IRQ_RX_HALF :
            (hal_event == HAL_UART_RXEVENT_TC) ? ARK_UART_IRQ_RX_COMPLETE :
                                                ARK_UART_IRQ_RX_IDLE;
    ark_uart_manage_handle_rx_event(id, event, size);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *handle) {
    ark_hal_uart_id_t id;

    if (stm32f1_uart_resolve_id(handle, &id)) {
        ark_uart_manage_handle_irq(id, ARK_UART_IRQ_ERROR, handle->ErrorCode);
    }
}
