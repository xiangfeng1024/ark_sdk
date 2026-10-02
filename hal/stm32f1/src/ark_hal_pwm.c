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

#include "ark_hal_pwm.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "tim.h"
#include "ark_hal_bindings.h"

static StaticSemaphore_t pwm_mutex_control;
static StaticSemaphore_t pwm_done_control;
static SemaphoreHandle_t pwm_mutex;
static SemaphoreHandle_t pwm_done;
static volatile bool pwm_dma_has_failed;

static TIM_HandleTypeDef *stm32f1_pwm_handle(ark_hal_pwm_id_t id) {
    return (id < ARK_HAL_PWM_COUNT) ? (TIM_HandleTypeDef *)ark_hal_pwm_handles[id] : NULL;
}

static bool stm32f1_pwm_is_channel_valid(uint32_t channel) {
    return (channel == TIM_CHANNEL_1) ||
           (channel == TIM_CHANNEL_2) ||
           (channel == TIM_CHANNEL_3) ||
           (channel == TIM_CHANNEL_4);
}

static bool stm32f1_pwm_sync_init(void) {
    if (pwm_mutex == NULL) {
        pwm_mutex = xSemaphoreCreateMutexStatic(&pwm_mutex_control);
        pwm_done = xSemaphoreCreateBinaryStatic(&pwm_done_control);
    }
    return (pwm_mutex != NULL) && (pwm_done != NULL);
}

static bool stm32f1_pwm_is_ready(ark_hal_pwm_id_t id) {
    TIM_HandleTypeDef *handle = stm32f1_pwm_handle(id);

    return (handle != NULL) && (handle->Instance != NULL) &&
           (handle->State != HAL_TIM_STATE_RESET);
}

static uint32_t stm32f1_pwm_period_ticks(ark_hal_pwm_id_t id) {
    TIM_HandleTypeDef *handle = stm32f1_pwm_handle(id);

    return (handle == NULL) ? 0U : handle->Init.Period + 1U;
}

static bool stm32f1_pwm_start(ark_hal_pwm_id_t id) {
    TIM_HandleTypeDef *handle = stm32f1_pwm_handle(id);
    uint32_t channel = (id < ARK_HAL_PWM_COUNT) ? ark_hal_pwm_channels[id] : 0U;

    return (handle != NULL) && stm32f1_pwm_is_channel_valid(channel) &&
           (HAL_TIM_PWM_Start(handle, channel) == HAL_OK);
}

static bool stm32f1_pwm_stop(ark_hal_pwm_id_t id) {
    TIM_HandleTypeDef *handle = stm32f1_pwm_handle(id);
    uint32_t channel = (id < ARK_HAL_PWM_COUNT) ? ark_hal_pwm_channels[id] : 0U;

    return (handle != NULL) && stm32f1_pwm_is_channel_valid(channel) &&
           (HAL_TIM_PWM_Stop(handle, channel) == HAL_OK);
}

static bool stm32f1_pwm_set_compare(ark_hal_pwm_id_t id, uint32_t compare) {
    TIM_HandleTypeDef *handle = stm32f1_pwm_handle(id);
    uint32_t channel = (id < ARK_HAL_PWM_COUNT) ? ark_hal_pwm_channels[id] : 0U;

    if ((handle == NULL) || !stm32f1_pwm_is_channel_valid(channel) ||
        (compare > stm32f1_pwm_period_ticks(id))) {
        return false;
    }
    __HAL_TIM_SET_COMPARE(handle, channel, compare);
    return true;
}

static int32_t stm32f1_pwm_transmit_dma(
    ark_hal_pwm_id_t id,
    const uint16_t *values,
    size_t count,
    uint32_t timeout_ms) {
    TIM_HandleTypeDef *handle = stm32f1_pwm_handle(id);
    uint32_t channel = (id < ARK_HAL_PWM_COUNT) ? ark_hal_pwm_channels[id] : 0U;
    int32_t result = -1;

    if ((handle == NULL) || !stm32f1_pwm_is_channel_valid(channel) ||
        (values == NULL) || (count == 0U) || (count > UINT16_MAX) ||
        !stm32f1_pwm_sync_init() ||
        (xSemaphoreTake(pwm_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)) {
        return -1;
    }
    (void)xSemaphoreTake(pwm_done, 0U);
    pwm_dma_has_failed = false;
    if ((HAL_TIM_PWM_Start_DMA(
            handle,
            channel,
            (uint32_t *)values,
            (uint16_t)count) == HAL_OK) &&
        (xSemaphoreTake(pwm_done, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) &&
        !pwm_dma_has_failed) {
        result = (int32_t)count;
    }
    (void)HAL_TIM_PWM_Stop_DMA(handle, channel);
    (void)xSemaphoreGive(pwm_mutex);
    return result;
}

static void stm32f1_pwm_complete(
    TIM_HandleTypeDef *handle,
    bool has_failed) {
    BaseType_t higher_priority_task_woken = pdFALSE;
    size_t index;

    for (index = 0U; index < ARK_HAL_PWM_COUNT; ++index) {
        if (stm32f1_pwm_handle((ark_hal_pwm_id_t)index) == handle) {
            pwm_dma_has_failed = has_failed;
            xSemaphoreGiveFromISR(pwm_done, &higher_priority_task_woken);
            portYIELD_FROM_ISR(higher_priority_task_woken);
            return;
        }
    }
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *handle) {
    stm32f1_pwm_complete(handle, false);
}

void HAL_TIM_ErrorCallback(TIM_HandleTypeDef *handle) {
    stm32f1_pwm_complete(handle, true);
}

const ark_hal_pwm_driver_t ark_hal_pwm = {
    .is_ready = stm32f1_pwm_is_ready,
    .period_ticks = stm32f1_pwm_period_ticks,
    .start = stm32f1_pwm_start,
    .stop = stm32f1_pwm_stop,
    .set_compare = stm32f1_pwm_set_compare,
    .transmit_dma = stm32f1_pwm_transmit_dma,
};
