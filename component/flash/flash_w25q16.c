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

#include "flash_w25q16.h"

#include "ark_dts_generated.h"

#if ARK_DTS_HAS_FLASH_W25Q16

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "ark_component.h"
#include "ark_dts.h"
#include "ark_hal_gpio.h"
#include "ark_hal_spi.h"
#include "flash_backend.h"

#define W25Q16_CMD_JEDEC_ID 0x9FU
#define W25Q16_CMD_READ 0x03U
#define W25Q16_CMD_PAGE_PROGRAM 0x02U
#define W25Q16_CMD_SECTOR_ERASE 0x20U
#define W25Q16_CMD_WRITE_ENABLE 0x06U
#define W25Q16_CMD_STATUS_1 0x05U
#define W25Q16_STATUS_BUSY 0x01U
#define W25Q16_PAGE_SIZE 256U
#define W25Q16_TIMEOUT_MS 100U
#define W25Q16_ERASE_TIMEOUT_MS 500U
#define W25Q16_SELF_TEST_EXPECTED_MS 800U

typedef struct {
    ark_hal_spi_id_t spi_id;
    ark_hal_gpio_config_t CS;
} w25q16_config_t;

static uint8_t w25q16_tx_buffer[W25Q16_PAGE_SIZE + 4U];
static uint8_t w25q16_rx_buffer[W25Q16_PAGE_SIZE + 4U];
static uint8_t w25q16_compare_buffer[W25Q16_PAGE_SIZE];
static w25q16_config_t w25q16_config;
static bool flash_w25q16_ready;

static bool w25q16_load_config(void) {
    const ark_of_node_t *node =
        ark_of_find_compatible_node(NULL, "flash_w25q16");
    uint8_t spi_id;

    if ((node == NULL) ||
        (ark_of_hal_get_parent(node, ARK_OF_HAL_SPI, &spi_id) != 0) ||
        (ark_of_get_named_gpio(node, "cs-gpios", 0U,
                              &w25q16_config.CS) != 0)) {
        return false;
    }
    w25q16_config.spi_id = (ark_hal_spi_id_t)spi_id;
    return true;
}

static void w25q16_select(bool selected) {
    ark_hal_gpio.set(w25q16_config.CS.port, w25q16_config.CS.pin,
                    selected ? (w25q16_config.CS.active_level != 0U) :
                               (w25q16_config.CS.active_level == 0U));
}

static bool w25q16_exchange(const uint8_t *tx, uint8_t *rx, size_t size) {
    int32_t result;

    w25q16_select(true);
    result = ark_hal_spi.transmit_receive(
        w25q16_config.spi_id, tx, rx, size, W25Q16_TIMEOUT_MS);
    w25q16_select(false);
    return result == (int32_t)size;
}

static bool w25q16_command(const uint8_t *command, size_t size) {
    int32_t result;

    w25q16_select(true);
    result = ark_hal_spi.transmit(
        w25q16_config.spi_id, command, size, W25Q16_TIMEOUT_MS);
    w25q16_select(false);
    return result == (int32_t)size;
}

static bool w25q16_write_enable(void) {
    const uint8_t command = W25Q16_CMD_WRITE_ENABLE;

    return w25q16_command(&command, 1U);
}

static bool w25q16_wait_ready(uint32_t timeout_ms) {
    TickType_t started = xTaskGetTickCount();
    uint8_t tx[2] = {W25Q16_CMD_STATUS_1, 0xFFU};
    uint8_t rx[2];

    for (;;) {
        if (!w25q16_exchange(tx, rx, sizeof(tx))) {
            return false;
        }
        if ((rx[1] & W25Q16_STATUS_BUSY) == 0U) {
            return true;
        }
        if ((xTaskGetTickCount() - started) >= pdMS_TO_TICKS(timeout_ms)) {
            return false;
        }
        vTaskDelay(1U);
    }
}

bool flash_w25q16_read_id(uint8_t id[3]) {
    uint8_t tx[4] = {W25Q16_CMD_JEDEC_ID, 0xFFU, 0xFFU, 0xFFU};
    uint8_t rx[4];

    if ((id == NULL) || !w25q16_exchange(tx, rx, sizeof(tx))) {
        return false;
    }
    memcpy(id, &rx[1], 3U);
    return true;
}

static bool flash_w25q16_read(
    uint32_t address,
    void *data,
    size_t size) {
    uint8_t *output = data;
    if ((data == NULL) || (address >= FLASH_W25Q16_CAPACITY_BYTES) ||
        (size > FLASH_W25Q16_CAPACITY_BYTES - address)) {
        return false;
    }
    while (size > 0U) {
        size_t chunk = (size > W25Q16_PAGE_SIZE) ? W25Q16_PAGE_SIZE : size;

        w25q16_tx_buffer[0] = W25Q16_CMD_READ;
        w25q16_tx_buffer[1] = (uint8_t)(address >> 16U);
        w25q16_tx_buffer[2] = (uint8_t)(address >> 8U);
        w25q16_tx_buffer[3] = (uint8_t)address;
        memset(&w25q16_tx_buffer[4], 0xFF, chunk);
        if (!w25q16_exchange(w25q16_tx_buffer, w25q16_rx_buffer, chunk + 4U)) {
            return false;
        }
        memcpy(output, &w25q16_rx_buffer[4], chunk);
        address += (uint32_t)chunk;
        output += chunk;
        size -= chunk;
    }
    return true;
}

static bool flash_w25q16_erase_4k(uint32_t address) {
    uint8_t command[4];

    if (address >= FLASH_W25Q16_CAPACITY_BYTES) {
        return false;
    }
    address &= ~(FLASH_W25Q16_SECTOR_SIZE - 1U);
    command[0] = W25Q16_CMD_SECTOR_ERASE;
    command[1] = (uint8_t)(address >> 16U);
    command[2] = (uint8_t)(address >> 8U);
    command[3] = (uint8_t)address;
    return w25q16_write_enable() && w25q16_command(command, sizeof(command)) &&
           w25q16_wait_ready(W25Q16_ERASE_TIMEOUT_MS);
}

static bool w25q16_needs_erase(uint32_t address, const uint8_t *data, size_t size) {
    while (size > 0U) {
        size_t chunk = (size > sizeof(w25q16_compare_buffer)) ?
                       sizeof(w25q16_compare_buffer) : size;
        size_t index;

        if (!flash_w25q16_read(address, w25q16_compare_buffer, chunk)) {
            return true;
        }
        for (index = 0U; index < chunk; ++index) {
            if ((w25q16_compare_buffer[index] & data[index]) != data[index]) {
                return true;
            }
        }
        address += (uint32_t)chunk;
        data += chunk;
        size -= chunk;
    }
    return false;
}

static bool flash_w25q16_write(
    uint32_t address,
    const void *data,
    size_t size) {
    const uint8_t *input = data;
    uint32_t erase_address;
    if ((data == NULL) || (address >= FLASH_W25Q16_CAPACITY_BYTES) ||
        (size > FLASH_W25Q16_CAPACITY_BYTES - address)) {
        return false;
    }
    if (size == 0U) {
        return true;
    }
    erase_address = address & ~(FLASH_W25Q16_SECTOR_SIZE - 1U);
    while (erase_address <= (address + (uint32_t)size - 1U)) {
        uint32_t begin = (address > erase_address) ? address : erase_address;
        uint32_t end = address + (uint32_t)size;
        uint32_t sector_end = erase_address + FLASH_W25Q16_SECTOR_SIZE;

        if (end > sector_end) {
            end = sector_end;
        }
        if (w25q16_needs_erase(begin, input + (begin - address), end - begin) &&
            !flash_w25q16_erase_4k(erase_address)) {
            return false;
        }
        erase_address += FLASH_W25Q16_SECTOR_SIZE;
    }
    while (size > 0U) {
        size_t page_space = W25Q16_PAGE_SIZE - (address & (W25Q16_PAGE_SIZE - 1U));
        size_t chunk = (size < page_space) ? size : page_space;

        w25q16_tx_buffer[0] = W25Q16_CMD_PAGE_PROGRAM;
        w25q16_tx_buffer[1] = (uint8_t)(address >> 16U);
        w25q16_tx_buffer[2] = (uint8_t)(address >> 8U);
        w25q16_tx_buffer[3] = (uint8_t)address;
        memcpy(&w25q16_tx_buffer[4], input, chunk);
        if (!w25q16_write_enable() ||
            !w25q16_command(w25q16_tx_buffer, chunk + 4U) ||
            !w25q16_wait_ready(W25Q16_TIMEOUT_MS)) {
            return false;
        }
        address += (uint32_t)chunk;
        input += chunk;
        size -= chunk;
    }
    return true;
}

static bool flash_w25q16_erase(
    uint32_t address,
    size_t size) {
    uint32_t end;

    if ((size == 0U) || (address >= FLASH_W25Q16_CAPACITY_BYTES) ||
        (size > FLASH_W25Q16_CAPACITY_BYTES - address)) {
        return false;
    }
    end = address + (uint32_t)size;
    address &= ~(FLASH_W25Q16_SECTOR_SIZE - 1U);
    while (address < end) {
        if (!flash_w25q16_erase_4k(address)) {
            return false;
        }
        address += FLASH_W25Q16_SECTOR_SIZE;
    }
    return true;
}

static bool flash_w25q16_is_ready(void) {
    return flash_w25q16_ready;
}

static const flash_backend_t flash_w25q16_backend = {
    .name = "w25q16",
    .capacity = FLASH_W25Q16_CAPACITY_BYTES,
    .erase_block_size = FLASH_W25Q16_SECTOR_SIZE,
    .is_ready = flash_w25q16_is_ready,
    .read = flash_w25q16_read,
    .write = flash_w25q16_write,
    .erase = flash_w25q16_erase,
};

static ark_component_result_t flash_w25q16_init(void) {
    uint8_t id[3];

    if (!w25q16_load_config()) {
        return ARK_COMPONENT_ERROR;
    }
    w25q16_select(false);
    if (!ark_hal_spi.is_ready(w25q16_config.spi_id) ||
        !flash_w25q16_read_id(id)) {
        return ARK_COMPONENT_ERROR;
    }
    flash_w25q16_ready = true;
    return flash_backend_attach(&flash_w25q16_backend) ?
           ARK_COMPONENT_OK : ARK_COMPONENT_ERROR;
}

static ark_component_result_t flash_w25q16_self_test(void) {
    static const uint32_t test_address =
        FLASH_W25Q16_CAPACITY_BYTES - FLASH_W25Q16_SECTOR_SIZE;
    static const uint8_t expected[] = "ARK-W25Q16-SELFTEST";
    uint8_t actual[sizeof(expected)];
    uint8_t id[3];

    if (!flash_w25q16_read_id(id)) {
        return ARK_COMPONENT_ERROR;
    }
    if ((id[0] != 0xEFU) || (id[1] != 0x40U) || (id[2] != 0x15U)) {
        return ARK_COMPONENT_ERROR;
    }
    if (!flash_w25q16_erase_4k(test_address) ||
        !flash_w25q16_write(test_address, expected, sizeof(expected)) ||
        !flash_w25q16_read(test_address, actual, sizeof(actual)) ||
        (memcmp(expected, actual, sizeof(expected)) != 0)) {
        return ARK_COMPONENT_ERROR;
    }
    return ARK_COMPONENT_OK;
}

static ark_component_t flash_w25q16_component = {
    .name = "flash_w25q16",
    .init = flash_w25q16_init,
    .self_test = flash_w25q16_self_test,
    .self_test_expected_ms = W25Q16_SELF_TEST_EXPECTED_MS,
    .init_level = ARK_COMPONENT_INIT_LEVEL_2,
};

bool flash_w25q16_register(void) {
    return ark_component_register(&flash_w25q16_component);
}

#endif
