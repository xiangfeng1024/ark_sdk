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

#ifndef ARK_DTS_H
#define ARK_DTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ark_hal_gpio.h"

#define ARK_OF_INVALID_INDEX 0xFFU
#define ARK_OF_MAX_PHANDLE_ARGS 4U

typedef enum {
    ARK_OF_PROP_BOOL = 0,
    ARK_OF_PROP_U32,
    ARK_OF_PROP_STRING,
    ARK_OF_PROP_BYTES
} ark_of_property_type_t;

typedef enum {
    ARK_OF_HAL_GPIO = 0,
    ARK_OF_HAL_ADC,
    ARK_OF_HAL_CAN,
    ARK_OF_HAL_ENCODER,
    ARK_OF_HAL_I2C,
    ARK_OF_HAL_PWM,
    ARK_OF_HAL_SPI,
    ARK_OF_HAL_UART,
    ARK_OF_HAL_WATCHDOG,
    ARK_OF_HAL_USB_DEVICE,
    ARK_OF_HAL_COUNT
} ark_of_hal_kind_t;

typedef struct {
    uint16_t path_hash;
    uint8_t parent;
    uint8_t first_child;
    uint8_t next_sibling;
    uint8_t property_index;
    uint8_t property_count;
    uint8_t phandle;
} ark_of_node_t;

typedef struct {
    uint16_t name_hash;
    uint16_t data_offset;
    uint8_t length;
    uint8_t type;
} ark_of_property_t;

typedef struct {
    uint8_t node_index;
    uint8_t kind;
    uint8_t id;
} ark_of_hal_ref_t;

typedef struct {
    const ark_of_node_t *np;
    uint32_t args[ARK_OF_MAX_PHANDLE_ARGS];
    size_t args_count;
} ark_of_phandle_args_t;

typedef struct {
    const ark_of_node_t *nodes;
    size_t node_count;
    const ark_of_property_t *properties;
    size_t property_count;
    const uint8_t *data;
    const ark_of_hal_ref_t *hal_refs;
    size_t hal_ref_count;
} ark_of_blob_t;

extern const ark_of_blob_t ark_of_blob;

const ark_of_node_t *ark_of_root(void);
const ark_of_node_t *ark_of_find_node_by_path(const char *path);
const ark_of_node_t *ark_of_find_compatible_node(
    const ark_of_node_t *from,
    const char *compatible);
const ark_of_node_t *ark_of_get_parent(const ark_of_node_t *node);
const ark_of_node_t *ark_of_get_next_available_child(
    const ark_of_node_t *parent,
    const ark_of_node_t *previous);
bool ark_of_device_is_available(const ark_of_node_t *node);
bool ark_of_property_read_bool(const ark_of_node_t *node, const char *name);
int32_t ark_of_property_read_u32(
    const ark_of_node_t *node, const char *name, uint32_t *value);
int32_t ark_of_property_read_s32(
    const ark_of_node_t *node, const char *name, int32_t *value);
int32_t ark_of_property_read_u32_array(
    const ark_of_node_t *node,
    const char *name,
    uint32_t *values,
    size_t count);
int32_t ark_of_property_read_string(
    const ark_of_node_t *node, const char *name, const char **value);
int32_t ark_of_property_read_string_index(
    const ark_of_node_t *node,
    const char *name,
    size_t index,
    const char **value);
int32_t ark_of_property_count_elems(
    const ark_of_node_t *node,
    const char *name,
    size_t element_size);
int32_t ark_of_parse_phandle_with_args(
    const ark_of_node_t *node,
    const char *name,
    const char *cells_name,
    size_t index,
    ark_of_phandle_args_t *result);
int32_t ark_of_get_named_gpio(
    const ark_of_node_t *node,
    const char *name,
    size_t index,
    ark_hal_gpio_config_t *gpio);
int32_t ark_of_hal_get_parent(
    const ark_of_node_t *node,
    ark_of_hal_kind_t kind,
    uint8_t *id);
int32_t ark_of_hal_get(
    const ark_of_node_t *node,
    const char *name,
    size_t index,
    ark_of_hal_kind_t kind,
    uint8_t *id,
    uint32_t *argument);

#endif /* ARK_DTS_H */
