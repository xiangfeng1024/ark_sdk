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

#include "ark_hal_gpio.h"

#include <stddef.h>

#include "stm32f1xx_hal.h"

#define STM32F1_GPIO_PIN_COUNT 16U
#define STM32F1_GPIO_BSRR_RESET_SHIFT 16U
#define STM32F1_GPIO_NANOSECONDS_PER_SECOND 1000000000ULL
#define STM32F1_GPIO_MSB_MASK 0x80U

typedef struct {
    ark_hal_gpio_port_t port;
    ark_hal_gpio_irq_callback_t callback;
    void *context;
} stm32f1_gpio_irq_slot_t;

static stm32f1_gpio_irq_slot_t gpio_irq_slots[STM32F1_GPIO_PIN_COUNT];

static GPIO_TypeDef *stm32f1_gpio_resolve_port(ark_hal_gpio_port_t port) {
    switch (port) {
        case ARK_HAL_GPIO_PORT_A:
            return GPIOA;

        case ARK_HAL_GPIO_PORT_B:
            return GPIOB;

        case ARK_HAL_GPIO_PORT_C:
            return GPIOC;

        case ARK_HAL_GPIO_PORT_D:
            return GPIOD;

#if defined(GPIOE)
        case ARK_HAL_GPIO_PORT_E:
            return GPIOE;

#endif
#if defined(GPIOF)
        case ARK_HAL_GPIO_PORT_F:
            return GPIOF;

#endif
#if defined(GPIOG)
        case ARK_HAL_GPIO_PORT_G:
            return GPIOG;

#endif
        default:
            return NULL;
    }
}

static void stm32f1_gpio_set(
    ark_hal_gpio_port_t port,
    ark_hal_gpio_pin_t pin,
    bool is_high) {
    GPIO_TypeDef *gpio = stm32f1_gpio_resolve_port(port);

    if (gpio != NULL) {
        gpio->BSRR = is_high ? pin :
            ((uint32_t)pin << STM32F1_GPIO_BSRR_RESET_SHIFT);
    }
}

static bool stm32f1_gpio_configure(
    ark_hal_gpio_port_t port,
    ark_hal_gpio_pin_t pin,
    ark_hal_gpio_mode_t mode,
    bool is_pull_up_enabled) {
    GPIO_TypeDef *gpio = stm32f1_gpio_resolve_port(port);
    GPIO_InitTypeDef gpio_config = {0};

    if (gpio == NULL) {
        return false;
    }
    gpio_config.Pin = pin;
    gpio_config.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio_config.Pull = is_pull_up_enabled ? GPIO_PULLUP : GPIO_NOPULL;
    gpio_config.Mode =
        (mode == ARK_HAL_GPIO_MODE_INPUT) ? GPIO_MODE_INPUT :
        (mode == ARK_HAL_GPIO_MODE_OUTPUT_OPEN_DRAIN) ? GPIO_MODE_OUTPUT_OD :
        GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(gpio, &gpio_config);
    return true;
}

static bool stm32f1_gpio_get(ark_hal_gpio_port_t port, ark_hal_gpio_pin_t pin) {
    GPIO_TypeDef *gpio = stm32f1_gpio_resolve_port(port);

    return (gpio != NULL) && (HAL_GPIO_ReadPin(gpio, pin) == GPIO_PIN_SET);
}

static void stm32f1_gpio_toggle(ark_hal_gpio_port_t port, ark_hal_gpio_pin_t pin) {
    GPIO_TypeDef *gpio = stm32f1_gpio_resolve_port(port);

    if (gpio != NULL) {
        HAL_GPIO_TogglePin(gpio, pin);
    }
}

static bool stm32f1_gpio_transmit_pulse_bytes(
    ark_hal_gpio_port_t port,
    ark_hal_gpio_pin_t pin,
    const uint8_t *data,
    size_t size,
    uint32_t period_ns,
    uint32_t zero_high_ns,
    uint32_t one_high_ns) {
    GPIO_TypeDef *gpio = stm32f1_gpio_resolve_port(port);
    uint32_t period_cycles;
    uint32_t zero_high_cycles;
    uint32_t one_high_cycles;
    size_t index;

    if ((gpio == NULL) || (pin == 0U) || (data == NULL) || (size == 0U) ||
        (zero_high_ns >= period_ns) || (one_high_ns >= period_ns)) {
        return false;
    }
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    period_cycles = (uint32_t)(
        ((uint64_t)SystemCoreClock * period_ns) /
        STM32F1_GPIO_NANOSECONDS_PER_SECOND);
    zero_high_cycles = (uint32_t)(
        ((uint64_t)SystemCoreClock * zero_high_ns) /
        STM32F1_GPIO_NANOSECONDS_PER_SECOND);
    one_high_cycles = (uint32_t)(
        ((uint64_t)SystemCoreClock * one_high_ns) /
        STM32F1_GPIO_NANOSECONDS_PER_SECOND);
    if ((period_cycles == 0U) || (zero_high_cycles == 0U) ||
        (one_high_cycles == 0U)) {
        return false;
    }

    for (index = 0U; index < size; ++index) {
        uint32_t mask;

        for (mask = STM32F1_GPIO_MSB_MASK; mask != 0U; mask >>= 1U) {
            uint32_t start_cycle = DWT->CYCCNT;
            uint32_t high_cycles = (data[index] & mask) ?
                                   one_high_cycles : zero_high_cycles;

            gpio->BSRR = pin;
            while ((uint32_t)(DWT->CYCCNT - start_cycle) < high_cycles) {
            }
            gpio->BSRR =
                (uint32_t)pin << STM32F1_GPIO_BSRR_RESET_SHIFT;
            while ((uint32_t)(DWT->CYCCNT - start_cycle) < period_cycles) {
            }
        }
    }
    return true;
}

static int32_t stm32f1_gpio_get_pin_index(ark_hal_gpio_pin_t pin) {
    int32_t index;

    if ((pin == 0U) || ((pin & (pin - 1U)) != 0U)) {
        return -1;
    }
    for (index = 0; index < (int32_t)STM32F1_GPIO_PIN_COUNT; ++index) {
        if (pin == ARK_HAL_GPIO_PIN((uint32_t)index)) {
            return index;
        }
    }
    return -1;
}

static bool stm32f1_gpio_register_irq(
    ark_hal_gpio_port_t port,
    ark_hal_gpio_pin_t pin,
    ark_hal_gpio_irq_callback_t callback,
    void *context) {
    int32_t index = stm32f1_gpio_get_pin_index(pin);

    if ((index < 0) || (stm32f1_gpio_resolve_port(port) == NULL) ||
        (callback == NULL) || (gpio_irq_slots[index].callback != NULL)) {
        return false;
    }
    gpio_irq_slots[index].port = port;
    gpio_irq_slots[index].context = context;
    gpio_irq_slots[index].callback = callback;
    return true;
}

const ark_hal_gpio_driver_t ark_hal_gpio = {
    .set = stm32f1_gpio_set,
    .get = stm32f1_gpio_get,
    .toggle = stm32f1_gpio_toggle,
    .transmit_pulse_bytes = stm32f1_gpio_transmit_pulse_bytes,
    .configure = stm32f1_gpio_configure,
    .register_irq = stm32f1_gpio_register_irq,
};

void HAL_GPIO_EXTI_Callback(uint16_t gpio_pin) {
    int32_t index = stm32f1_gpio_get_pin_index((ark_hal_gpio_pin_t)gpio_pin);

    if ((index >= 0) && (gpio_irq_slots[index].callback != NULL)) {
        gpio_irq_slots[index].callback(gpio_irq_slots[index].context);
    }
}
