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

#include "flash_stm32f103.h"

#include "ark_dts_generated.h"

#if ARK_DTS_HAS_FLASH_STM32F103

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "stm32f1xx_hal.h"
#include "ark_component.h"
#include "flash_backend.h"

static bool flash_stm32f103_ready;

static bool flash_stm32f103_read(
    uint32_t address,
    void *data,
    size_t size) {
    if ((data == NULL) || (address > FLASH_STM32F103_STORAGE_SIZE) ||
        (size > FLASH_STM32F103_STORAGE_SIZE - address)) {
        return false;
    }
    memcpy(
        data,
        (const void *)(FLASH_STM32F103_STORAGE_ADDRESS + address),
        size);
    return true;
}

static bool flash_stm32f103_erase_page(void) {
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0U;
    bool success;

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = FLASH_STM32F103_ERASE_PAGE_ADDRESS;
    erase.NbPages = 1U;
    success = (HAL_FLASH_Unlock() == HAL_OK) &&
              (HAL_FLASHEx_Erase(&erase, &page_error) == HAL_OK);
    if (HAL_FLASH_Lock() != HAL_OK) {
        success = false;
    }
    return success;
}

static bool flash_stm32f103_write(
    uint32_t address,
    const void *data,
    size_t size) {
    const uint8_t *bytes = data;
    uint32_t offset;
    bool success = true;

    if ((data == NULL) || (size == 0U) ||
        (address != 0U) || (size > FLASH_STM32F103_STORAGE_SIZE)) {
        return false;
    }
    vTaskSuspendAll();
    if (!flash_stm32f103_erase_page() || (HAL_FLASH_Unlock() != HAL_OK)) {
        success = false;
    }
    for (offset = 0U; success && (offset < size); offset += 2U) {
        uint16_t halfword = bytes[offset];

        halfword |= ((offset + 1U) < size) ?
                    (uint16_t)bytes[offset + 1U] << 8U : 0xFF00U;
        if (HAL_FLASH_Program(
                FLASH_TYPEPROGRAM_HALFWORD,
                FLASH_STM32F103_STORAGE_ADDRESS + offset,
                halfword) != HAL_OK) {
            success = false;
        }
    }
    if (HAL_FLASH_Lock() != HAL_OK) {
        success = false;
    }
    (void)xTaskResumeAll();
    return success && (memcmp(
               data,
               (const void *)FLASH_STM32F103_STORAGE_ADDRESS,
               size) == 0);
}

static bool flash_stm32f103_erase(uint32_t address, size_t size) {
    bool success;

    if ((address != 0U) || (size == 0U) ||
        (size > FLASH_STM32F103_STORAGE_SIZE)) {
        return false;
    }
    vTaskSuspendAll();
    success = flash_stm32f103_erase_page();
    (void)xTaskResumeAll();
    return success;
}

static bool flash_stm32f103_is_ready(void) {
    return flash_stm32f103_ready;
}

static const flash_backend_t flash_stm32f103_backend = {
    .name = "internal",
    .capacity = FLASH_STM32F103_STORAGE_SIZE,
    .erase_block_size = FLASH_STM32F103_ERASE_PAGE_SIZE,
    .is_ready = flash_stm32f103_is_ready,
    .read = flash_stm32f103_read,
    .write = flash_stm32f103_write,
    .erase = flash_stm32f103_erase,
};

static ark_component_result_t flash_stm32f103_init(void) {
    flash_stm32f103_ready = true;
    return flash_backend_attach(&flash_stm32f103_backend) ?
           ARK_COMPONENT_OK : ARK_COMPONENT_ERROR;
}

static const ark_component_t flash_stm32f103_component = {
    .name = "flash_stm32f103",
    .init = flash_stm32f103_init,
    .self_test = NULL,
    .self_test_expected_ms = 0U,
    .init_level = ARK_COMPONENT_INIT_LEVEL_2,
};

bool flash_stm32f103_register(void) {
    return ark_component_register(&flash_stm32f103_component);
}

#endif
