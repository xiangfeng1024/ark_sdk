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

#include "ark_hal_can.h"

#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "can.h"
#include "ark_hal_bindings.h"

#define STM32F1_CAN_RX_QUEUE_LENGTH 4U
#define STM32F1_CAN_CONTROL_TIMEOUT_MS 100U
#define STM32F1_CAN_STANDARD_ID_MAX 0x7FFU
#define STM32F1_CAN_EXTENDED_ID_MAX 0x1FFFFFFFU
#define STM32F1_CAN_MAX_DATA_LENGTH 8U
#define STM32F1_CAN_STANDARD_ID_REGISTER_SHIFT 5U
#define STM32F1_CAN_TX_WAIT_TICKS 1U

static StaticSemaphore_t can_mutex_control;
static SemaphoreHandle_t can_mutex;
static StaticQueue_t can_rx_queue_control;
static uint8_t can_rx_queue_storage[
    STM32F1_CAN_RX_QUEUE_LENGTH * sizeof(ark_hal_can_frame_t)];
static QueueHandle_t can_rx_queue;
static uint16_t can_filter_id[ARK_HAL_CAN_COUNT];
static bool can_is_filter_configured[ARK_HAL_CAN_COUNT];
static volatile uint32_t can_received_count;
static volatile uint32_t can_dropped_count;

static CAN_HandleTypeDef *stm32f1_can_handle(ark_hal_can_id_t id) {
    return (id < ARK_HAL_CAN_COUNT) ? (CAN_HandleTypeDef *)ark_hal_can_handles[id] : NULL;
}

static bool stm32f1_can_lock(uint32_t timeout_ms) {
    if (can_mutex == NULL) {
        can_mutex = xSemaphoreCreateMutexStatic(&can_mutex_control);
        can_rx_queue = xQueueCreateStatic(
            STM32F1_CAN_RX_QUEUE_LENGTH,
            sizeof(ark_hal_can_frame_t),
            can_rx_queue_storage,
            &can_rx_queue_control);
    }
    return (can_mutex != NULL) &&
           (xSemaphoreTake(can_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE);
}

static int32_t stm32f1_can_apply_filter(ark_hal_can_id_t id) {
    CAN_HandleTypeDef *handle = stm32f1_can_handle(id);
    CAN_FilterTypeDef filter_config;

    if ((handle == NULL) || !can_is_filter_configured[id]) {
        return -1;
    }
    memset(&filter_config, 0, sizeof(filter_config));
    filter_config.FilterBank = 0U;
    filter_config.FilterMode = CAN_FILTERMODE_IDMASK;
    filter_config.FilterScale = CAN_FILTERSCALE_32BIT;
    filter_config.FilterIdHigh =
        (uint32_t)can_filter_id[id] << STM32F1_CAN_STANDARD_ID_REGISTER_SHIFT;
    filter_config.FilterIdLow = 0U;
    filter_config.FilterMaskIdHigh =
        STM32F1_CAN_STANDARD_ID_MAX << STM32F1_CAN_STANDARD_ID_REGISTER_SHIFT;
    filter_config.FilterMaskIdLow = 0x0006U;
    filter_config.FilterFIFOAssignment = CAN_RX_FIFO1;
    filter_config.FilterActivation = ENABLE;
    return (HAL_CAN_ConfigFilter(handle, &filter_config) == HAL_OK) ? 0 : -1;
}

static int32_t stm32f1_can_configure_std_filter(
    ark_hal_can_id_t id,
    uint16_t standard_id) {
    if ((id >= ARK_HAL_CAN_COUNT) ||
        (standard_id > STM32F1_CAN_STANDARD_ID_MAX) ||
        !stm32f1_can_lock(STM32F1_CAN_CONTROL_TIMEOUT_MS)) {
        return -1;
    }
    can_filter_id[id] = standard_id;
    can_is_filter_configured[id] = true;
    if (stm32f1_can_apply_filter(id) != 0) {
        (void)xSemaphoreGive(can_mutex);
        return -1;
    }
    (void)xSemaphoreGive(can_mutex);
    return 0;
}

static bool stm32f1_can_is_ready(ark_hal_can_id_t id) {
    CAN_HandleTypeDef *handle = stm32f1_can_handle(id);

    return (handle != NULL) && (handle->Instance != NULL) &&
           (handle->State != HAL_CAN_STATE_RESET);
}

static int32_t stm32f1_can_set_loopback(ark_hal_can_id_t id, bool is_enabled) {
    CAN_HandleTypeDef *handle = stm32f1_can_handle(id);

    if ((handle == NULL) ||
        !stm32f1_can_lock(STM32F1_CAN_CONTROL_TIMEOUT_MS)) {
        return -1;
    }
    (void)HAL_CAN_Stop(handle);
    (void)HAL_CAN_DeInit(handle);
    handle->Init.Mode = is_enabled ? CAN_MODE_LOOPBACK : CAN_MODE_NORMAL;
    if (HAL_CAN_Init(handle) != HAL_OK) {
        (void)xSemaphoreGive(can_mutex);
        return -1;
    }
    (void)xSemaphoreGive(can_mutex);
    return 0;
}

static int32_t stm32f1_can_start(ark_hal_can_id_t id) {
    CAN_HandleTypeDef *handle = stm32f1_can_handle(id);

    if ((handle == NULL) ||
        !stm32f1_can_lock(STM32F1_CAN_CONTROL_TIMEOUT_MS)) {
        return -1;
    }
    if (HAL_CAN_GetState(handle) == HAL_CAN_STATE_LISTENING) {
        (void)xSemaphoreGive(can_mutex);
        return 0;
    }
    if (stm32f1_can_apply_filter(id) != 0) {
        (void)xSemaphoreGive(can_mutex);
        return -1;
    }
    if (HAL_CAN_Start(handle) != HAL_OK) {
        (void)xSemaphoreGive(can_mutex);
        return -1;
    }
    if (HAL_CAN_ActivateNotification(
            handle,
            CAN_IT_RX_FIFO1_MSG_PENDING) != HAL_OK) {
        (void)HAL_CAN_Stop(handle);
        (void)xSemaphoreGive(can_mutex);
        return -1;
    }
    (void)xSemaphoreGive(can_mutex);
    return 0;
}

static int32_t stm32f1_can_transmit(
    ark_hal_can_id_t id,
    const ark_hal_can_frame_t *frame,
    uint32_t timeout_ms) {
    CAN_HandleTypeDef *handle = stm32f1_can_handle(id);
    CAN_TxHeaderTypeDef header;
    uint32_t mailbox;
    TickType_t start_tick;
    HAL_StatusTypeDef hal_status;

    if ((handle == NULL) || (frame == NULL) ||
        (frame->length > STM32F1_CAN_MAX_DATA_LENGTH) ||
        !stm32f1_can_lock(timeout_ms)) {
        return -1;
    }
    header.StdId = frame->id & STM32F1_CAN_STANDARD_ID_MAX;
    header.ExtId = frame->id & STM32F1_CAN_EXTENDED_ID_MAX;
    header.IDE = frame->extended ? CAN_ID_EXT : CAN_ID_STD;
    header.RTR = frame->remote ? CAN_RTR_REMOTE : CAN_RTR_DATA;
    header.DLC = frame->length;
    header.TransmitGlobalTime = DISABLE;
    start_tick = xTaskGetTickCount();
    while (HAL_CAN_GetTxMailboxesFreeLevel(handle) == 0U) {
        if ((xTaskGetTickCount() - start_tick) >= pdMS_TO_TICKS(timeout_ms)) {
            (void)xSemaphoreGive(can_mutex);
            return -1;
        }
        vTaskDelay(STM32F1_CAN_TX_WAIT_TICKS);
    }
    hal_status = HAL_CAN_AddTxMessage(
        handle,
        &header,
        (uint8_t *)frame->data,
        &mailbox);
    (void)xSemaphoreGive(can_mutex);
    return (hal_status == HAL_OK) ? (int32_t)frame->length : -1;
}

static int32_t stm32f1_can_receive(
    ark_hal_can_id_t id,
    ark_hal_can_frame_t *frame,
    uint32_t timeout_ms) {
    if ((id >= ARK_HAL_CAN_COUNT) || (frame == NULL) || (can_rx_queue == NULL)) {
        return -1;
    }
    return (xQueueReceive(can_rx_queue, frame, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) ?
           (int32_t)frame->length : -1;
}

void HAL_CAN_RxFifo1MsgPendingCallback(CAN_HandleTypeDef *handle) {
    CAN_RxHeaderTypeDef header;
    ark_hal_can_frame_t frame;
    BaseType_t higher_priority_task_woken = pdFALSE;

    if ((can_rx_queue == NULL) ||
        (HAL_CAN_GetRxMessage(handle, CAN_RX_FIFO1, &header, frame.data) != HAL_OK)) {
        return;
    }
    frame.extended = header.IDE == CAN_ID_EXT;
    frame.remote = header.RTR == CAN_RTR_REMOTE;
    frame.id = frame.extended ? header.ExtId : header.StdId;
    frame.length = (uint8_t)header.DLC;
    if (xQueueSendFromISR(can_rx_queue, &frame, &higher_priority_task_woken) == pdTRUE) {
        ++can_received_count;
    } else {
        ++can_dropped_count;
    }
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

static bool stm32f1_can_get_diagnostics(
    ark_hal_can_id_t id,
    ark_hal_can_diagnostics_t *diagnostics) {
    CAN_HandleTypeDef *handle = stm32f1_can_handle(id);

    if ((handle == NULL) || (diagnostics == NULL)) {
        return false;
    }
    diagnostics->hal_state = (uint32_t)HAL_CAN_GetState(handle);
    diagnostics->hal_error = HAL_CAN_GetError(handle);
    diagnostics->esr = handle->Instance->ESR;
    diagnostics->tsr = handle->Instance->TSR;
    diagnostics->rf1r = handle->Instance->RF1R;
    diagnostics->tx_mailboxes_free = HAL_CAN_GetTxMailboxesFreeLevel(handle);
    diagnostics->received = can_received_count;
    diagnostics->dropped = can_dropped_count;
    return true;
}

const ark_hal_can_driver_t ark_hal_can = {
    .is_ready = stm32f1_can_is_ready,
    .configure_std_filter = stm32f1_can_configure_std_filter,
    .set_loopback = stm32f1_can_set_loopback,
    .start = stm32f1_can_start,
    .transmit = stm32f1_can_transmit,
    .receive = stm32f1_can_receive,
    .get_diagnostics = stm32f1_can_get_diagnostics,
};
