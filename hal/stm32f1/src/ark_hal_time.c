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

#include "ark_hal_time.h"

#include "stm32f1xx_hal.h"

#define STM32F1_TIME_MICROSECONDS_PER_SECOND 1000000U

static bool stm32f1_time_enable(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    return (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U;
}

static bool stm32f1_time_is_ready(void) {
    return ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U) ||
           stm32f1_time_enable();
}

static uint32_t stm32f1_time_frequency_hz(void) {
    return SystemCoreClock;
}

static uint32_t stm32f1_time_cycles(void) {
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U) {
        stm32f1_time_enable();
    }
    return DWT->CYCCNT;
}

static void stm32f1_time_delay_us(uint32_t microseconds) {
    uint32_t delay_cycles =
        (SystemCoreClock / STM32F1_TIME_MICROSECONDS_PER_SECOND) * microseconds;
    uint32_t start_cycle = stm32f1_time_cycles();

    while ((uint32_t)(stm32f1_time_cycles() - start_cycle) < delay_cycles) {
    }
}

const ark_hal_time_driver_t ark_hal_time = {
    .is_ready = stm32f1_time_is_ready,
    .frequency_hz = stm32f1_time_frequency_hz,
    .cycles = stm32f1_time_cycles,
    .delay_us = stm32f1_time_delay_us,
};
