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

#include "ark_dts.h"

#include <string.h>

static uint16_t ark_of_hash(const char *text) {
    uint32_t value = 2166136261UL;

    while ((text != NULL) && (*text != '\0')) {
        value ^= (uint8_t)*text++;
        value *= 16777619UL;
    }
    return (uint16_t)(value ^ (value >> 16U));
}

static const ark_of_property_t *ark_of_find_property(
    const ark_of_node_t *node,
    const char *name) {
    size_t index;

    if ((node == NULL) || (name == NULL)) {
        return NULL;
    }
    for (index = 0U; index < node->property_count; ++index) {
        const ark_of_property_t *property =
            &ark_of_blob.properties[node->property_index + index];

        if (property->name_hash == ark_of_hash(name)) {
            return property;
        }
    }
    return NULL;
}

static uint32_t ark_of_data_u32(size_t offset) {
    return (uint32_t)ark_of_blob.data[offset] |
           ((uint32_t)ark_of_blob.data[offset + 1U] << 8U) |
           ((uint32_t)ark_of_blob.data[offset + 2U] << 16U) |
           ((uint32_t)ark_of_blob.data[offset + 3U] << 24U);
}

static uint16_t ark_of_node_index(const ark_of_node_t *node) {
    if ((node == NULL) || (node < ark_of_blob.nodes) ||
        (node >= &ark_of_blob.nodes[ark_of_blob.node_count])) {
        return ARK_OF_INVALID_INDEX;
    }
    return (uint16_t)(node - ark_of_blob.nodes);
}

static const ark_of_node_t *ark_of_node_from_phandle(uint32_t phandle) {
    size_t index;

    for (index = 0U; index < ark_of_blob.node_count; ++index) {
        if (ark_of_blob.nodes[index].phandle == phandle) {
            return &ark_of_blob.nodes[index];
        }
    }
    return NULL;
}

static int32_t ark_of_hal_id(
    const ark_of_node_t *node,
    ark_of_hal_kind_t kind,
    uint8_t *id) {
    uint16_t node_index = ark_of_node_index(node);
    size_t index;

    if ((node_index == ARK_OF_INVALID_INDEX) || (id == NULL)) {
        return -1;
    }
    for (index = 0U; index < ark_of_blob.hal_ref_count; ++index) {
        const ark_of_hal_ref_t *ref = &ark_of_blob.hal_refs[index];

        if ((ref->node_index == node_index) && (ref->kind == (uint8_t)kind)) {
            *id = ref->id;
            return 0;
        }
    }
    return -1;
}

const ark_of_node_t *ark_of_root(void) {
    return (ark_of_blob.node_count == 0U) ? NULL : &ark_of_blob.nodes[0];
}

const ark_of_node_t *ark_of_find_node_by_path(const char *path) {
    uint16_t hash = ark_of_hash(path);
    size_t index;

    if (path == NULL) {
        return NULL;
    }
    for (index = 0U; index < ark_of_blob.node_count; ++index) {
        if (ark_of_blob.nodes[index].path_hash == hash) {
            return &ark_of_blob.nodes[index];
        }
    }
    return NULL;
}

int32_t ark_of_property_read_string_index(
    const ark_of_node_t *node,
    const char *name,
    size_t index,
    const char **value) {
    const ark_of_property_t *property = ark_of_find_property(node, name);
    const char *current;
    size_t offset = 0U;
    size_t item = 0U;

    if ((property == NULL) || (property->type != ARK_OF_PROP_STRING) ||
        (value == NULL)) {
        return -1;
    }
    current = (const char *)&ark_of_blob.data[property->data_offset];
    while (offset < property->length) {
        size_t length = strlen(current);

        if (item == index) {
            *value = current;
            return 0;
        }
        offset += length + 1U;
        current += length + 1U;
        ++item;
    }
    return -1;
}

const ark_of_node_t *ark_of_find_compatible_node(
    const ark_of_node_t *from,
    const char *compatible) {
    size_t index = (from == NULL) ? 0U : (size_t)ark_of_node_index(from) + 1U;

    if (compatible == NULL) {
        return NULL;
    }
    for (; index < ark_of_blob.node_count; ++index) {
        const ark_of_node_t *node = &ark_of_blob.nodes[index];
        size_t string_index = 0U;
        const char *value;

        while (ark_of_property_read_string_index(
                   node, "compatible", string_index, &value) == 0) {
            if ((strcmp(value, compatible) == 0) && ark_of_device_is_available(node)) {
                return node;
            }
            ++string_index;
        }
    }
    return NULL;
}

const ark_of_node_t *ark_of_get_parent(const ark_of_node_t *node) {
    if ((ark_of_node_index(node) == ARK_OF_INVALID_INDEX) ||
        (node->parent == ARK_OF_INVALID_INDEX)) {
        return NULL;
    }
    return &ark_of_blob.nodes[node->parent];
}

const ark_of_node_t *ark_of_get_next_available_child(
    const ark_of_node_t *parent,
    const ark_of_node_t *previous) {
    uint16_t index;

    if (ark_of_node_index(parent) == ARK_OF_INVALID_INDEX) {
        return NULL;
    }
    index = (previous == NULL) ? parent->first_child : previous->next_sibling;
    while (index != ARK_OF_INVALID_INDEX) {
        const ark_of_node_t *child = &ark_of_blob.nodes[index];

        if (ark_of_device_is_available(child)) {
            return child;
        }
        index = child->next_sibling;
    }
    return NULL;
}

bool ark_of_device_is_available(const ark_of_node_t *node) {
    const char *status;

    return (node != NULL) &&
        ((ark_of_property_read_string(node, "status", &status) != 0) ||
         (strcmp(status, "okay") == 0) || (strcmp(status, "ok") == 0));
}

bool ark_of_property_read_bool(const ark_of_node_t *node, const char *name) {
    return ark_of_find_property(node, name) != NULL;
}

int32_t ark_of_property_read_u32_array(
    const ark_of_node_t *node,
    const char *name,
    uint32_t *values,
    size_t count) {
    const ark_of_property_t *property = ark_of_find_property(node, name);
    size_t index;

    if ((property == NULL) || (property->type != ARK_OF_PROP_U32) ||
        (values == NULL) || (property->length < count * sizeof(uint32_t))) {
        return -1;
    }
    for (index = 0U; index < count; ++index) {
        values[index] = ark_of_data_u32(property->data_offset + index * 4U);
    }
    return 0;
}

int32_t ark_of_property_read_u32(
    const ark_of_node_t *node, const char *name, uint32_t *value) {
    return ark_of_property_read_u32_array(node, name, value, 1U);
}

int32_t ark_of_property_read_s32(
    const ark_of_node_t *node, const char *name, int32_t *value) {
    uint32_t raw;

    if ((value == NULL) || (ark_of_property_read_u32(node, name, &raw) != 0)) {
        return -1;
    }
    *value = (int32_t)raw;
    return 0;
}

int32_t ark_of_property_read_string(
    const ark_of_node_t *node, const char *name, const char **value) {
    return ark_of_property_read_string_index(node, name, 0U, value);
}

int32_t ark_of_property_count_elems(
    const ark_of_node_t *node,
    const char *name,
    size_t element_size) {
    const ark_of_property_t *property = ark_of_find_property(node, name);

    if ((property == NULL) || (element_size == 0U) ||
        ((property->length % element_size) != 0U)) {
        return -1;
    }
    return (int32_t)(property->length / element_size);
}

int32_t ark_of_parse_phandle_with_args(
    const ark_of_node_t *node,
    const char *name,
    const char *cells_name,
    size_t index,
    ark_of_phandle_args_t *result) {
    const ark_of_property_t *property = ark_of_find_property(node, name);
    size_t data_offset = 0U;
    size_t entry_index = 0U;

    if ((property == NULL) || (property->type != ARK_OF_PROP_U32) ||
        (cells_name == NULL) || (result == NULL)) {
        return -1;
    }
    while (data_offset + 4U <= property->length) {
        const ark_of_node_t *provider = ark_of_node_from_phandle(
            ark_of_data_u32(property->data_offset + data_offset));
        uint32_t cell_count;

        if ((provider == NULL) ||
            (ark_of_property_read_u32(provider, cells_name, &cell_count) != 0) ||
            (cell_count > ARK_OF_MAX_PHANDLE_ARGS) ||
            (data_offset + (cell_count + 1U) * 4U > property->length)) {
            return -1;
        }
        if (entry_index == index) {
            size_t argument_index;

            result->np = provider;
            result->args_count = cell_count;
            for (argument_index = 0U;
                 argument_index < cell_count;
                 ++argument_index) {
                result->args[argument_index] = ark_of_data_u32(
                    property->data_offset + data_offset +
                    (argument_index + 1U) * 4U);
            }
            return 0;
        }
        data_offset += (cell_count + 1U) * 4U;
        ++entry_index;
    }
    return -1;
}

int32_t ark_of_get_named_gpio(
    const ark_of_node_t *node,
    const char *name,
    size_t index,
    ark_hal_gpio_config_t *gpio) {
    ark_of_phandle_args_t args;
    uint8_t port;

    if ((gpio == NULL) ||
        (ark_of_parse_phandle_with_args(node, name, "#gpio-cells", index, &args) != 0) ||
        (args.args_count != 2U) ||
        (ark_of_hal_id(args.np, ARK_OF_HAL_GPIO, &port) != 0) ||
        (port > ARK_HAL_GPIO_PORT_G) || (args.args[0] > 15U)) {
        return -1;
    }
    gpio->port = (ark_hal_gpio_port_t)port;
    gpio->pin = ARK_HAL_GPIO_PIN(args.args[0]);
    gpio->active_level = (uint8_t)(args.args[1] == 0U ? 1U : 0U);
    return 0;
}

int32_t ark_of_hal_get_parent(
    const ark_of_node_t *node,
    ark_of_hal_kind_t kind,
    uint8_t *id) {
    return ark_of_hal_id(ark_of_get_parent(node), kind, id);
}

int32_t ark_of_hal_get(
    const ark_of_node_t *node,
    const char *name,
    size_t index,
    ark_of_hal_kind_t kind,
    uint8_t *id,
    uint32_t *argument) {
    ark_of_phandle_args_t args;
    const char *cells_name =
        (kind == ARK_OF_HAL_GPIO) ? "#gpio-cells" :
        (kind == ARK_OF_HAL_PWM) ? "#pwm-cells" :
        (kind == ARK_OF_HAL_ADC) ? "#io-channel-cells" : "#ark,hal-cells";

    if ((ark_of_parse_phandle_with_args(node, name, cells_name, index, &args) != 0) ||
        (ark_of_hal_id(args.np, kind, id) != 0)) {
        return -1;
    }
    if (argument != NULL) {
        *argument = (args.args_count == 0U) ? 0U : args.args[0];
    }
    return 0;
}
