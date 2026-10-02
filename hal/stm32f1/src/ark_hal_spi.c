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

#include "ark_hal_spi.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "spi.h"
#include "ark_hal_bindings.h"

static StaticSemaphore_t spi_mutex_control;
static StaticSemaphore_t spi_dma_done_control;
static SemaphoreHandle_t spi_mutex;
static SemaphoreHandle_t spi_dma_done;
static volatile bool spi_dma_has_failed;

static SPI_HandleTypeDef *stm32f1_spi_handle(ark_hal_spi_id_t id) {
    return (id < ARK_HAL_SPI_COUNT) ?
           (SPI_HandleTypeDef *)ark_hal_spi_handles[id] : NULL;
}

static bool stm32f1_spi_sync_init(void) {
    if (spi_mutex == NULL) {
        spi_mutex = xSemaphoreCreateMutexStatic(&spi_mutex_control);
        spi_dma_done = xSemaphoreCreateBinaryStatic(&spi_dma_done_control);
    }
    return (spi_mutex != NULL) && (spi_dma_done != NULL);
}

static bool stm32f1_spi_is_ready(ark_hal_spi_id_t id) {
    SPI_HandleTypeDef *handle = stm32f1_spi_handle(id);

    return (handle != NULL) && (handle->Instance != NULL) &&
           (handle->State != HAL_SPI_STATE_RESET);
}

static int32_t stm32f1_spi_transmit(
    ark_hal_spi_id_t id,
    const uint8_t *data,
    size_t size,
    uint32_t timeout_ms) {
    SPI_HandleTypeDef *handle = stm32f1_spi_handle(id);
    int32_t result = -1;

    if ((handle == NULL) || (data == NULL) || (size == 0U) ||
        (size > UINT16_MAX) || !stm32f1_spi_sync_init() ||
        (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)) {
        return -1;
    }
    if (HAL_SPI_Transmit(
            handle,
            (uint8_t *)data,
            (uint16_t)size,
            timeout_ms) == HAL_OK) {
        result = (int32_t)size;
    }
    (void)xSemaphoreGive(spi_mutex);
    return result;
}

static int32_t stm32f1_spi_transmit_dma(
    ark_hal_spi_id_t id,
    const uint8_t *data,
    size_t size,
    uint32_t timeout_ms) {
    SPI_HandleTypeDef *handle = stm32f1_spi_handle(id);
    int32_t result = -1;

    if ((handle == NULL) || (handle->hdmatx == NULL) || (data == NULL) ||
        (size == 0U) || (size > UINT16_MAX) || !stm32f1_spi_sync_init() ||
        (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)) {
        return -1;
    }
    (void)xSemaphoreTake(spi_dma_done, 0U);
    spi_dma_has_failed = false;
    if ((HAL_SPI_Transmit_DMA(handle, (uint8_t *)data, (uint16_t)size) == HAL_OK) &&
        (xSemaphoreTake(spi_dma_done, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) &&
        !spi_dma_has_failed) {
        result = (int32_t)size;
    }
    if (result < 0) {
        (void)HAL_SPI_Abort(handle);
    }
    (void)xSemaphoreGive(spi_mutex);
    return result;
}

static int32_t stm32f1_spi_transmit_receive(
    ark_hal_spi_id_t id,
    const uint8_t *tx_data,
    uint8_t *rx_data,
    size_t size,
    uint32_t timeout_ms) {
    SPI_HandleTypeDef *handle = stm32f1_spi_handle(id);
    int32_t result = -1;

    if ((handle == NULL) || (tx_data == NULL) || (rx_data == NULL) ||
        (size == 0U) || (size > UINT16_MAX) || !stm32f1_spi_sync_init() ||
        (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)) {
        return -1;
    }
    if (HAL_SPI_TransmitReceive(
            handle,
            (uint8_t *)tx_data,
            rx_data,
            (uint16_t)size,
            timeout_ms) == HAL_OK) {
        result = (int32_t)size;
    }
    (void)xSemaphoreGive(spi_mutex);
    return result;
}

const ark_hal_spi_driver_t ark_hal_spi = {
    .is_ready = stm32f1_spi_is_ready,
    .transmit = stm32f1_spi_transmit,
    .transmit_receive = stm32f1_spi_transmit_receive,
    .transmit_dma = stm32f1_spi_transmit_dma,
};

static void stm32f1_spi_dma_complete(
    SPI_HandleTypeDef *handle,
    bool has_failed) {
    BaseType_t higher_priority_task_woken = pdFALSE;
    size_t index;
    bool is_matching_handle = false;

    for (index = 0U; index < ARK_HAL_SPI_COUNT; ++index) {
        SPI_HandleTypeDef *configured_handle =
            stm32f1_spi_handle((ark_hal_spi_id_t)index);

        if ((configured_handle == handle) ||
            ((configured_handle != NULL) &&
             (configured_handle->Instance == handle->Instance))) {
            is_matching_handle = true;
            break;
        }
    }
    if (!is_matching_handle) {
        return;
    }
    spi_dma_has_failed = has_failed;
    xSemaphoreGiveFromISR(spi_dma_done, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *handle) {
    stm32f1_spi_dma_complete(handle, false);
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *handle) {
    stm32f1_spi_dma_complete(handle, true);
}
