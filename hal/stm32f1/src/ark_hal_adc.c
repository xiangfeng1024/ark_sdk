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

#include "ark_hal_adc.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "adc.h"
#include "ark_hal_bindings.h"

static StaticSemaphore_t adc_mutex_control;
static SemaphoreHandle_t adc_mutex;
static bool adc_is_calibrated[ARK_HAL_ADC_COUNT];

static ADC_HandleTypeDef *stm32f1_adc_handle(ark_hal_adc_id_t id) {
    return (id < ARK_HAL_ADC_COUNT) ?
        (ADC_HandleTypeDef *)ark_hal_adc_handles[id] : NULL;
}

static bool stm32f1_adc_is_ready(ark_hal_adc_id_t id) {
    ADC_HandleTypeDef *handle = stm32f1_adc_handle(id);

    return (handle != NULL) && (handle->Instance != NULL) &&
           (handle->State != HAL_ADC_STATE_RESET);
}

static bool stm32f1_adc_read(
    ark_hal_adc_id_t id,
    uint32_t channel,
    uint16_t *value,
    uint32_t timeout_ms) {
    ADC_HandleTypeDef *handle = stm32f1_adc_handle(id);
    ADC_ChannelConfTypeDef channel_config = {0};
    bool result = false;

    if ((handle == NULL) || (value == NULL) || (channel > ADC_CHANNEL_17)) {
        return false;
    }
    if (adc_mutex == NULL) {
        adc_mutex = xSemaphoreCreateMutexStatic(&adc_mutex_control);
    }
    if ((adc_mutex == NULL) ||
        (xSemaphoreTake(adc_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)) {
        return false;
    }
    channel_config.Channel = channel;
    channel_config.Rank = ADC_REGULAR_RANK_1;
    channel_config.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;
    /* The public read API is single-channel. ADC1 also has a CubeMX scan
     * configuration for the board's four inputs, so force one conversion
     * before each dynamically selected read to prevent rank spillover. */
    handle->Init.ScanConvMode = ADC_SCAN_DISABLE;
    handle->Init.NbrOfConversion = 1U;
    MODIFY_REG(handle->Instance->CR1, ADC_CR1_SCAN, 0U);
    MODIFY_REG(handle->Instance->SQR1, ADC_SQR1_L, 0U);
    if (!adc_is_calibrated[id] &&
        (HAL_ADCEx_Calibration_Start(handle) == HAL_OK)) {
        adc_is_calibrated[id] = true;
    }
    if (adc_is_calibrated[id] &&
        (HAL_ADC_ConfigChannel(handle, &channel_config) == HAL_OK) &&
        (HAL_ADC_Start(handle) == HAL_OK) &&
        (HAL_ADC_PollForConversion(handle, timeout_ms) == HAL_OK)) {
        *value = (uint16_t)HAL_ADC_GetValue(handle);
        result = true;
    }
    (void)HAL_ADC_Stop(handle);
    (void)xSemaphoreGive(adc_mutex);
    return result;
}

const ark_hal_adc_driver_t ark_hal_adc = {
    .is_ready = stm32f1_adc_is_ready,
    .read = stm32f1_adc_read,
};
