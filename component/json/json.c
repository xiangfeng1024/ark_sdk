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

#include "json.h"

#include <limits.h>
#include <string.h>

#include "FreeRTOS.h"
#include "ark_component.h"

#define JSON_SELF_TEST_EXPECTED_MS 10U

static bool is_json_initialized;

static void *json_allocate(size_t size)
{
    return pvPortMalloc(size);
}

static void json_release(void *pointer)
{
    vPortFree(pointer);
}

bool json_initialize(void)
{
    cJSON_Hooks hooks;

    if (is_json_initialized) {
        return true;
    }

    hooks.malloc_fn = json_allocate;
    hooks.free_fn = json_release;
    cJSON_InitHooks(&hooks);
    is_json_initialized = true;
    return true;
}

json_value_t *json_create_object(void)
{
    return is_json_initialized ? cJSON_CreateObject() : NULL;
}

json_value_t *json_parse(const char *text, size_t length)
{
    if (!is_json_initialized || (text == NULL) || (length == 0U) ||
        (length > (size_t)INT_MAX)) {
        return NULL;
    }

    return cJSON_ParseWithLengthOpts(text, length, NULL, 0);
}

void json_delete(json_value_t *value)
{
    if (value != NULL) {
        cJSON_Delete(value);
    }
}

static const json_value_t *json_get_object_item(const json_value_t *object,
                                                const char *name)
{
    if ((object == NULL) || (name == NULL) || !cJSON_IsObject(object)) {
        return NULL;
    }

    return cJSON_GetObjectItemCaseSensitive(object, name);
}

bool json_get_bool(const json_value_t *object, const char *name, bool *value)
{
    const json_value_t *json_item = json_get_object_item(object, name);

    if ((json_item == NULL) || (value == NULL) || !cJSON_IsBool(json_item)) {
        return false;
    }

    *value = cJSON_IsTrue(json_item);
    return true;
}

bool json_get_u32(const json_value_t *object, const char *name, uint32_t *value)
{
    const json_value_t *json_item = json_get_object_item(object, name);
    double number_value;

    if ((json_item == NULL) || (value == NULL) || !cJSON_IsNumber(json_item)) {
        return false;
    }

    number_value = json_item->valuedouble;
    if ((number_value < 0.0) || (number_value > (double)UINT32_MAX) ||
        ((double)(uint32_t)number_value != number_value)) {
        return false;
    }

    *value = (uint32_t)number_value;
    return true;
}

bool json_get_i32(const json_value_t *object, const char *name, int32_t *value)
{
    const json_value_t *json_item = json_get_object_item(object, name);
    double number_value;

    if ((json_item == NULL) || (value == NULL) || !cJSON_IsNumber(json_item)) {
        return false;
    }

    number_value = json_item->valuedouble;
    if ((number_value < (double)INT32_MIN) || (number_value > (double)INT32_MAX) ||
        ((double)(int32_t)number_value != number_value)) {
        return false;
    }

    *value = (int32_t)number_value;
    return true;
}

bool json_get_double(const json_value_t *object, const char *name, double *value)
{
    const json_value_t *json_item = json_get_object_item(object, name);

    if ((json_item == NULL) || (value == NULL) || !cJSON_IsNumber(json_item)) {
        return false;
    }

    *value = json_item->valuedouble;
    return true;
}

bool json_add_bool(json_value_t *object, const char *name, bool value)
{
    return (object != NULL) && (name != NULL) &&
           (cJSON_AddBoolToObject(object, name, value ? 1 : 0) != NULL);
}

bool json_add_u32(json_value_t *object, const char *name, uint32_t value)
{
    return (object != NULL) && (name != NULL) &&
           (cJSON_AddNumberToObject(object, name, (double)value) != NULL);
}

bool json_add_double(json_value_t *object, const char *name, double value)
{
    return (object != NULL) && (name != NULL) &&
           (cJSON_AddNumberToObject(object, name, value) != NULL);
}

bool json_print(const json_value_t *value,
                char *buffer,
                size_t capacity,
                size_t *length)
{
    int print_result;

    if ((value == NULL) || (buffer == NULL) || (capacity < 2U) ||
        (capacity > (size_t)INT_MAX)) {
        return false;
    }

    memset(buffer, 0, capacity);
    print_result = cJSON_PrintPreallocated((cJSON *)value, buffer, (int)capacity, 0);
    if (print_result == 0) {
        return false;
    }

    if (length != NULL) {
        *length = strlen(buffer);
    }
    return true;
}

static ark_component_result_t json_init(void)
{
    return json_initialize() ? ARK_COMPONENT_OK : ARK_COMPONENT_ERROR;
}

static ark_component_result_t json_self_test(void)
{
    static const char input[] = "{\"ok\":true,\"value\":7}";
    json_value_t *value;
    bool is_ok = false;
    uint32_t number_value = 0U;

    value = json_parse(input, sizeof(input) - 1U);
    if (value != NULL) {
        is_ok = json_get_bool(value, "ok", &is_ok) && is_ok &&
                json_get_u32(value, "value", &number_value) &&
                (number_value == 7U);
        json_delete(value);
    }
    return is_ok ? ARK_COMPONENT_OK : ARK_COMPONENT_ERROR;
}

static const ark_component_t json_component = {
    .name = "json",
    .init = json_init,
    .self_test = json_self_test,
    .self_test_expected_ms = JSON_SELF_TEST_EXPECTED_MS,
    .init_level = ARK_COMPONENT_INIT_LEVEL_1,
};

bool json_register(void)
{
    return ark_component_register(&json_component);
}
