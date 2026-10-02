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

#include "ark_hal_soft_i2c.h"

#include "i2c.h"
#include "stm32f1xx_hal.h"
#include "ark_hal_bindings.h"

#define STM32F1_SOFT_I2C_MICROSECONDS_PER_SECOND 1000000U
#define STM32F1_SOFT_I2C_BYTE_BIT_COUNT 8U
#define STM32F1_SOFT_I2C_MSB_MASK 0x80U

static const ark_hal_soft_i2c_config_t *stm32f1_soft_i2c_get_config(void) {
    return &ark_hal_soft_i2c_configs[ARK_HAL_SOFT_I2C_1];
}

static GPIO_TypeDef *stm32f1_soft_i2c_resolve_port(ark_hal_gpio_port_t port) {
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

static void stm32f1_soft_i2c_enable_port_clock(ark_hal_gpio_port_t port) {
    switch (port) {
        case ARK_HAL_GPIO_PORT_A:
            __HAL_RCC_GPIOA_CLK_ENABLE();
            break;

        case ARK_HAL_GPIO_PORT_B:
            __HAL_RCC_GPIOB_CLK_ENABLE();
            break;

        case ARK_HAL_GPIO_PORT_C:
            __HAL_RCC_GPIOC_CLK_ENABLE();
            break;

        case ARK_HAL_GPIO_PORT_D:
            __HAL_RCC_GPIOD_CLK_ENABLE();
            break;

#if defined(GPIOE)
        case ARK_HAL_GPIO_PORT_E:
            __HAL_RCC_GPIOE_CLK_ENABLE();
            break;

#endif
#if defined(GPIOF)
        case ARK_HAL_GPIO_PORT_F:
            __HAL_RCC_GPIOF_CLK_ENABLE();
            break;

#endif
#if defined(GPIOG)
        case ARK_HAL_GPIO_PORT_G:
            __HAL_RCC_GPIOG_CLK_ENABLE();
            break;

#endif
        default:
            break;
    }
}

static void stm32f1_soft_i2c_delay(void) {
    uint32_t start_cycle = DWT->CYCCNT;
    uint32_t delay_cycles =
        (HAL_RCC_GetHCLKFreq() /
         STM32F1_SOFT_I2C_MICROSECONDS_PER_SECOND) *
        stm32f1_soft_i2c_get_config()->delay_us;

    while ((DWT->CYCCNT - start_cycle) < delay_cycles) {
        __NOP();
    }
}

static void stm32f1_soft_i2c_scl(bool is_high) {
    const ark_hal_gpio_config_t *gpio = &stm32f1_soft_i2c_get_config()->scl;

    HAL_GPIO_WritePin(
        stm32f1_soft_i2c_resolve_port(gpio->port),
        gpio->pin,
        is_high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void stm32f1_soft_i2c_sda(bool is_high) {
    const ark_hal_gpio_config_t *gpio = &stm32f1_soft_i2c_get_config()->sda;

    HAL_GPIO_WritePin(
        stm32f1_soft_i2c_resolve_port(gpio->port),
        gpio->pin,
        is_high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static bool stm32f1_soft_i2c_sda_read(void) {
    const ark_hal_gpio_config_t *gpio = &stm32f1_soft_i2c_get_config()->sda;

    return HAL_GPIO_ReadPin(
        stm32f1_soft_i2c_resolve_port(gpio->port),
        gpio->pin) == GPIO_PIN_SET;
}

static void stm32f1_soft_i2c_start(void) {
    stm32f1_soft_i2c_sda(true);
    stm32f1_soft_i2c_scl(true);
    stm32f1_soft_i2c_delay();
    stm32f1_soft_i2c_sda(false);
    stm32f1_soft_i2c_delay();
    stm32f1_soft_i2c_scl(false);
}

static void stm32f1_soft_i2c_stop(void) {
    stm32f1_soft_i2c_sda(false);
    stm32f1_soft_i2c_delay();
    stm32f1_soft_i2c_scl(true);
    stm32f1_soft_i2c_delay();
    stm32f1_soft_i2c_sda(true);
    stm32f1_soft_i2c_delay();
}

static bool stm32f1_soft_i2c_write_byte(uint8_t value) {
    uint8_t bit;
    bool is_acknowledged;

    for (bit = 0U; bit < STM32F1_SOFT_I2C_BYTE_BIT_COUNT; ++bit) {
        stm32f1_soft_i2c_scl(false);
        stm32f1_soft_i2c_sda((value & STM32F1_SOFT_I2C_MSB_MASK) != 0U);
        stm32f1_soft_i2c_delay();
        stm32f1_soft_i2c_scl(true);
        stm32f1_soft_i2c_delay();
        value <<= 1U;
    }

    stm32f1_soft_i2c_scl(false);
    stm32f1_soft_i2c_sda(true);
    stm32f1_soft_i2c_delay();
    stm32f1_soft_i2c_scl(true);
    stm32f1_soft_i2c_delay();
    is_acknowledged = !stm32f1_soft_i2c_sda_read();
    stm32f1_soft_i2c_scl(false);
    stm32f1_soft_i2c_delay();
    return is_acknowledged;
}

static bool stm32f1_soft_i2c_init(ark_hal_soft_i2c_id_t id) {
    GPIO_InitTypeDef gpio_config = {0};
    const ark_hal_soft_i2c_config_t *soft_i2c_config = stm32f1_soft_i2c_get_config();
    GPIO_TypeDef *scl_port;
    GPIO_TypeDef *sda_port;

    if (id != ARK_HAL_SOFT_I2C_1) {
        return false;
    }

    scl_port = stm32f1_soft_i2c_resolve_port(soft_i2c_config->scl.port);
    sda_port = stm32f1_soft_i2c_resolve_port(soft_i2c_config->sda.port);
    if ((scl_port == NULL) || (sda_port == NULL) || (soft_i2c_config->delay_us == 0U)) {
        return false;
    }
    if (soft_i2c_config->deinit_hardware_i2c &&
        (soft_i2c_config->hardware_i2c_id < ARK_HAL_I2C_COUNT) &&
        (ark_hal_i2c_handles[soft_i2c_config->hardware_i2c_id] != NULL)) {
        HAL_I2C_DeInit(
            (I2C_HandleTypeDef *)
                ark_hal_i2c_handles[soft_i2c_config->hardware_i2c_id]);
    }

    stm32f1_soft_i2c_enable_port_clock(soft_i2c_config->scl.port);
    stm32f1_soft_i2c_enable_port_clock(soft_i2c_config->sda.port);

    gpio_config.Mode = GPIO_MODE_OUTPUT_OD;
    gpio_config.Pull = GPIO_NOPULL;
    gpio_config.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio_config.Pin = soft_i2c_config->scl.pin;
    HAL_GPIO_Init(scl_port, &gpio_config);
    gpio_config.Pin = soft_i2c_config->sda.pin;
    HAL_GPIO_Init(sda_port, &gpio_config);

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    stm32f1_soft_i2c_scl(true);
    stm32f1_soft_i2c_sda(true);
    stm32f1_soft_i2c_delay();
    return stm32f1_soft_i2c_sda_read();
}

static int32_t stm32f1_soft_i2c_transmit(
    ark_hal_soft_i2c_id_t id,
    uint8_t address,
    const uint8_t *data,
    size_t size) {
    size_t index;

    if ((id != ARK_HAL_SOFT_I2C_1) || (data == NULL) || (size == 0U)) {
        return -1;
    }

    stm32f1_soft_i2c_start();
    if (!stm32f1_soft_i2c_write_byte((uint8_t)(address << 1U))) {
        stm32f1_soft_i2c_stop();
        return -1;
    }
    for (index = 0U; index < size; ++index) {
        if (!stm32f1_soft_i2c_write_byte(data[index])) {
            stm32f1_soft_i2c_stop();
            return -1;
        }
    }
    stm32f1_soft_i2c_stop();
    return (int32_t)size;
}

const ark_hal_soft_i2c_driver_t ark_hal_soft_i2c = {
    .init = stm32f1_soft_i2c_init,
    .transmit = stm32f1_soft_i2c_transmit,
};
