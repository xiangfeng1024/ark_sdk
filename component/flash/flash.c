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

#include "flash.h"

#include <string.h>

#include "ark_component.h"
#include "ark_dts.h"
#include "flash_backend.h"

struct flash_device {
    const flash_backend_t *backend;
};

typedef struct {
    const ark_of_node_t *node;
    struct flash_device devices[4];
    size_t count;
    bool is_ready;
} flash_context_t;

static flash_context_t flash_context;

static bool flash_load_config(void) {
    const ark_of_node_t *node =
        ark_of_find_compatible_node(NULL, "flash");

    flash_context.node = node;
    return node != NULL;
}

bool flash_backend_attach(const flash_backend_t *backend) {
    if ((backend == NULL) || (backend->name == NULL) ||
        (backend->capacity == 0U) || (backend->is_ready == NULL) ||
        (backend->read == NULL) || (backend->write == NULL) ||
        (flash_context.count >= 4U) ||
        (flash_find(backend->name) != NULL)) {
        return false;
    }
    flash_context.devices[flash_context.count].backend = backend;
    flash_context.count++;
    return true;
}

static ark_component_result_t flash_init(void) {
    size_t index;

    if (!flash_load_config() || (flash_context.count == 0U)) {
        return ARK_COMPONENT_ERROR;
    }
    for (index = 0U; index < flash_context.count; ++index) {
        if (!flash_context.devices[index].backend->is_ready()) {
            return ARK_COMPONENT_ERROR;
        }
    }
    flash_context.is_ready = true;
    return ARK_COMPONENT_OK;
}

bool flash_is_ready(void) {
    return flash_context.is_ready;
}

const flash_device_t *flash_default(void) {
    return (flash_context.count == 0U) ? NULL : &flash_context.devices[0];
}

const flash_device_t *flash_find(const char *name) {
    size_t index;

    if (name == NULL) {
        return NULL;
    }
    for (index = 0U; index < flash_context.count; ++index) {
        if (strcmp(flash_context.devices[index].backend->name, name) == 0) {
            return &flash_context.devices[index];
        }
    }
    return NULL;
}

const char *flash_name(const flash_device_t *device) {
    return (device == NULL) ? NULL : device->backend->name;
}

size_t flash_capacity(const flash_device_t *device) {
    return (device == NULL) ? 0U : device->backend->capacity;
}

size_t flash_erase_block_size(const flash_device_t *device) {
    return (device == NULL) ? 0U : device->backend->erase_block_size;
}

bool flash_read(
    const flash_device_t *device,
    uint32_t address,
    void *data,
    size_t size) {
    return flash_is_ready() && (device != NULL) &&
           (data != NULL) &&
           (address <= device->backend->capacity) &&
           (size <= device->backend->capacity - address) &&
           device->backend->read(address, data, size);
}

bool flash_write(
    const flash_device_t *device,
    uint32_t address,
    const void *data,
    size_t size) {
    return flash_is_ready() && (device != NULL) &&
           (data != NULL) && (size != 0U) &&
           (address < device->backend->capacity) &&
           (size <= device->backend->capacity - address) &&
           device->backend->write(address, data, size);
}

bool flash_erase(
    const flash_device_t *device,
    uint32_t address,
    size_t size) {
    return flash_is_ready() && (device != NULL) &&
           (device->backend->erase != NULL) && (size != 0U) &&
           (address < device->backend->capacity) &&
           (size <= device->backend->capacity - address) &&
           device->backend->erase(address, size);
}

static const ark_component_t flash_component = {
    .name = "flash",
    .init = flash_init,
    .self_test = NULL,
    .self_test_expected_ms = 0U,
    .init_level = ARK_COMPONENT_INIT_LEVEL_3,
};

bool flash_register(void) {
    return ark_component_register(&flash_component);
}
