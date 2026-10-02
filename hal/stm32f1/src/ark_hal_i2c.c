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

#include "ark_hal_i2c.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "i2c.h"
#include "ark_hal_bindings.h"

static StaticSemaphore_t i2c_mutex_control;
static StaticSemaphore_t i2c_dma_done_control;
static SemaphoreHandle_t i2c_mutex;
static SemaphoreHandle_t i2c_dma_done;
static volatile bool i2c_dma_has_failed;

static I2C_HandleTypeDef *stm32f1_i2c_handle(ark_hal_i2c_id_t id) {
    return (id < ARK_HAL_I2C_COUNT) ?
           (I2C_HandleTypeDef *)ark_hal_i2c_handles[id] : NULL;
}

static bool stm32f1_i2c_sync_init(void) {
    if (i2c_mutex == NULL) {
        i2c_mutex = xSemaphoreCreateMutexStatic(&i2c_mutex_control);
        i2c_dma_done = xSemaphoreCreateBinaryStatic(&i2c_dma_done_control);
    }
    return (i2c_mutex != NULL) && (i2c_dma_done != NULL);
}

static bool stm32f1_i2c_is_ready(ark_hal_i2c_id_t id) {
    I2C_HandleTypeDef *handle = stm32f1_i2c_handle(id);

    return (handle != NULL) && (handle->Instance != NULL) &&
           (handle->State != HAL_I2C_STATE_RESET);
}

static bool stm32f1_i2c_is_device_ready(
    ark_hal_i2c_id_t id,
    uint8_t address,
    uint32_t timeout_ms) {
    I2C_HandleTypeDef *handle = stm32f1_i2c_handle(id);
    bool is_ready = false;

    if ((handle == NULL) || !stm32f1_i2c_sync_init() ||
        (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)) {
        return false;
    }
    is_ready = HAL_I2C_IsDeviceReady(
                handle, (uint16_t)address << 1, 2U, timeout_ms) == HAL_OK;
    (void)xSemaphoreGive(i2c_mutex);
    return is_ready;
}

static bool stm32f1_i2c_recover(ark_hal_i2c_id_t id, uint32_t timeout_ms) {
    I2C_HandleTypeDef *handle = stm32f1_i2c_handle(id);
    bool is_recovered = false;

    if ((handle == NULL) || !stm32f1_i2c_sync_init() ||
        (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)) {
        return false;
    }
    if ((HAL_I2C_DeInit(handle) == HAL_OK) &&
        (HAL_I2C_Init(handle) == HAL_OK)) {
        is_recovered = true;
    }
    (void)xSemaphoreGive(i2c_mutex);
    return is_recovered;
}

static int32_t stm32f1_i2c_transmit(
    ark_hal_i2c_id_t id,
    uint8_t address,
    const uint8_t *data,
    size_t size,
    uint32_t timeout_ms) {
    I2C_HandleTypeDef *handle = stm32f1_i2c_handle(id);
    int32_t result = -1;

    if ((handle == NULL) || (data == NULL) || (size == 0U) || (size > UINT16_MAX) ||
        !stm32f1_i2c_sync_init() ||
        (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)) {
        return -1;
    }
    if (HAL_I2C_Master_Transmit(
            handle,
            (uint16_t)address << 1,
            (uint8_t *)data,
            (uint16_t)size,
            timeout_ms) == HAL_OK) {
        result = (int32_t)size;
    }
    (void)xSemaphoreGive(i2c_mutex);
    return result;
}

static int32_t stm32f1_i2c_receive(
    ark_hal_i2c_id_t id,
    uint8_t address,
    uint8_t *data,
    size_t size,
    uint32_t timeout_ms) {
    I2C_HandleTypeDef *handle = stm32f1_i2c_handle(id);
    int32_t result = -1;

    if ((handle == NULL) || (data == NULL) || (size == 0U) || (size > UINT16_MAX) ||
        !stm32f1_i2c_sync_init() ||
        (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)) {
        return -1;
    }
    if (HAL_I2C_Master_Receive(
            handle,
            (uint16_t)address << 1,
            data,
            (uint16_t)size,
            timeout_ms) == HAL_OK) {
        result = (int32_t)size;
    }
    (void)xSemaphoreGive(i2c_mutex);
    return result;
}

static int32_t stm32f1_i2c_mem_write(
    ark_hal_i2c_id_t id,
    uint8_t address,
    uint8_t register_address,
    const uint8_t *data,
    size_t size,
    uint32_t timeout_ms) {
    I2C_HandleTypeDef *handle = stm32f1_i2c_handle(id);
    int32_t result = -1;

    if ((handle == NULL) || (data == NULL) || (size == 0U) || (size > UINT16_MAX) ||
        !stm32f1_i2c_sync_init() ||
        (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)) {
        return -1;
    }
    if (HAL_I2C_Mem_Write(
            handle,
            (uint16_t)address << 1,
            register_address,
            I2C_MEMADD_SIZE_8BIT,
            (uint8_t *)data,
            (uint16_t)size,
            timeout_ms) == HAL_OK) {
        result = (int32_t)size;
    }
    (void)xSemaphoreGive(i2c_mutex);
    return result;
}

static int32_t stm32f1_i2c_mem_read(
    ark_hal_i2c_id_t id,
    uint8_t address,
    uint8_t register_address,
    uint8_t *data,
    size_t size,
    uint32_t timeout_ms) {
    I2C_HandleTypeDef *handle = stm32f1_i2c_handle(id);
    int32_t result = -1;

    if ((handle == NULL) || (data == NULL) || (size == 0U) || (size > UINT16_MAX) ||
        !stm32f1_i2c_sync_init() ||
        (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)) {
        return -1;
    }
    if (HAL_I2C_Mem_Read(
            handle,
            (uint16_t)address << 1,
            register_address,
            I2C_MEMADD_SIZE_8BIT,
            data,
            (uint16_t)size,
            timeout_ms) == HAL_OK) {
        result = (int32_t)size;
    }
    (void)xSemaphoreGive(i2c_mutex);
    return result;
}

static int32_t stm32f1_i2c_dma_wait(size_t size, uint32_t timeout_ms) {
    if ((xSemaphoreTake(i2c_dma_done, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) &&
        !i2c_dma_has_failed) {
        return (int32_t)size;
    }
    return -1;
}

static int32_t stm32f1_i2c_transmit_dma(
    ark_hal_i2c_id_t id,
    uint8_t address,
    const uint8_t *data,
    size_t size,
    uint32_t timeout_ms) {
    I2C_HandleTypeDef *handle = stm32f1_i2c_handle(id);
    int32_t result = -1;

    if ((handle == NULL) || (handle->hdmatx == NULL) || (data == NULL) ||
        (size == 0U) || (size > UINT16_MAX) || !stm32f1_i2c_sync_init() ||
        (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)) {
        return -1;
    }
    (void)xSemaphoreTake(i2c_dma_done, 0U);
    i2c_dma_has_failed = false;
    if (HAL_I2C_Master_Transmit_DMA(
            handle,
            (uint16_t)address << 1,
            (uint8_t *)data,
            (uint16_t)size) == HAL_OK) {
        result = stm32f1_i2c_dma_wait(size, timeout_ms);
    }
    (void)xSemaphoreGive(i2c_mutex);
    return result;
}

static int32_t stm32f1_i2c_receive_dma(
    ark_hal_i2c_id_t id,
    uint8_t address,
    uint8_t *data,
    size_t size,
    uint32_t timeout_ms) {
    I2C_HandleTypeDef *handle = stm32f1_i2c_handle(id);
    int32_t result = -1;

    if ((handle == NULL) || (handle->hdmarx == NULL) || (data == NULL) ||
        (size == 0U) || (size > UINT16_MAX) || !stm32f1_i2c_sync_init() ||
        (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)) {
        return -1;
    }
    (void)xSemaphoreTake(i2c_dma_done, 0U);
    i2c_dma_has_failed = false;
    if (HAL_I2C_Master_Receive_DMA(
            handle,
            (uint16_t)address << 1,
            data,
            (uint16_t)size) == HAL_OK) {
        result = stm32f1_i2c_dma_wait(size, timeout_ms);
    }
    (void)xSemaphoreGive(i2c_mutex);
    return result;
}

const ark_hal_i2c_driver_t ark_hal_i2c = {
    .is_ready = stm32f1_i2c_is_ready,
    .is_device_ready = stm32f1_i2c_is_device_ready,
    .recover = stm32f1_i2c_recover,
    .transmit = stm32f1_i2c_transmit,
    .receive = stm32f1_i2c_receive,
    .mem_write = stm32f1_i2c_mem_write,
    .mem_read = stm32f1_i2c_mem_read,
    .transmit_dma = stm32f1_i2c_transmit_dma,
    .receive_dma = stm32f1_i2c_receive_dma,
};

static void stm32f1_i2c_dma_complete(
    I2C_HandleTypeDef *handle,
    bool has_failed) {
    BaseType_t higher_priority_task_woken = pdFALSE;
    size_t index;
    bool is_matching_handle = false;

    for (index = 0U; index < ARK_HAL_I2C_COUNT; ++index) {
        I2C_HandleTypeDef *configured_handle =
            stm32f1_i2c_handle((ark_hal_i2c_id_t)index);

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
    i2c_dma_has_failed = has_failed;
    xSemaphoreGiveFromISR(i2c_dma_done, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *handle) {
    stm32f1_i2c_dma_complete(handle, false);
}

void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *handle) {
    stm32f1_i2c_dma_complete(handle, false);
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *handle) {
    stm32f1_i2c_dma_complete(handle, true);
}
