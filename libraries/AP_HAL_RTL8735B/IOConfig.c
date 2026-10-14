/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "IOConfig.h"

#include "hwdef.h"
#include <PinNames.h>

// 表格來自開發板的 hwdef（rtl8735b_hwdef.py 產生的 HAL_RTL8735B_* 巨集）。
static const uint32_t rcout_pins[] = {HAL_RTL8735B_RCOUT_PINS};

static const struct {
    uint8_t logical_pin;
    uint32_t sdk_pin;
    uint8_t output_allowed;
} gpio_pins[] = {HAL_RTL8735B_GPIO_PINS};

static const struct {
    uint8_t logical_pin;
    uint32_t sdk_pin;
    uint8_t channel;
} adc_pins[] = {HAL_RTL8735B_ADC_PINS};

// 表的長度要等於產生器寫出的數量（手改 hwdef.h 時才可能不一致）
_Static_assert(sizeof(rcout_pins) / sizeof(rcout_pins[0]) == RTL8735B_RCOUT_CHANNELS,
               "RTL8735B RC output table and count differ");
_Static_assert(sizeof(gpio_pins) / sizeof(gpio_pins[0]) == RTL8735B_GPIO_PIN_COUNT,
               "RTL8735B GPIO table and count differ");
_Static_assert(sizeof(adc_pins) / sizeof(adc_pins[0]) == RTL8735B_ADC_CHANNELS,
               "RTL8735B ADC table and count differ");

uint32_t rtl8735b_rcout_pin(uint8_t channel)
{
    return channel < RTL8735B_RCOUT_CHANNELS ? rcout_pins[channel] : RTL8735B_INVALID_PIN;
}

uint32_t rtl8735b_gpio_pin(uint8_t logical_pin)
{
    for (uint8_t i = 0; i < RTL8735B_GPIO_PIN_COUNT; i++) {
        if (gpio_pins[i].logical_pin == logical_pin) {
            return gpio_pins[i].sdk_pin;
        }
    }
    return RTL8735B_INVALID_PIN;
}

uint8_t rtl8735b_gpio_output_allowed(uint8_t logical_pin)
{
    for (uint8_t i = 0; i < RTL8735B_GPIO_PIN_COUNT; i++) {
        if (gpio_pins[i].logical_pin == logical_pin) {
            return gpio_pins[i].output_allowed;
        }
    }
    return 0;
}

uint8_t rtl8735b_gpio_logical_pin(uint8_t slot)
{
    return slot < RTL8735B_GPIO_PIN_COUNT ? gpio_pins[slot].logical_pin : RTL8735B_INVALID_LOGICAL_PIN;
}

uint8_t rtl8735b_adc_logical_pin(uint8_t index)
{
    return index < RTL8735B_ADC_CHANNELS ? adc_pins[index].logical_pin : RTL8735B_INVALID_LOGICAL_PIN;
}

uint8_t rtl8735b_adc_channel(uint8_t logical_pin)
{
    for (uint8_t i = 0; i < RTL8735B_ADC_CHANNELS; i++) {
        if (adc_pins[i].logical_pin == logical_pin) {
            return adc_pins[i].channel;
        }
    }
    return RTL8735B_INVALID_LOGICAL_PIN;
}

uint32_t rtl8735b_analog_pin(uint8_t logical_pin)
{
    for (uint8_t i = 0; i < RTL8735B_ADC_CHANNELS; i++) {
        if (adc_pins[i].logical_pin == logical_pin) {
            return adc_pins[i].sdk_pin;
        }
    }
    return RTL8735B_INVALID_PIN;
}

uint8_t rtl8735b_rcout_channel_count(void)
{
    return RTL8735B_RCOUT_CHANNELS;
}

uint8_t rtl8735b_adc_channel_count(void)
{
    return RTL8735B_ADC_CHANNELS;
}

uint8_t rtl8735b_gpio_pin_count(void)
{
    return RTL8735B_GPIO_PIN_COUNT;
}

uint8_t rtl8735b_rc_rate_valid(uint16_t rate_hz)
{
    return rate_hz >= RTL8735B_RC_MIN_HZ && rate_hz <= RTL8735B_RC_MAX_HZ;
}

uint8_t rtl8735b_rc_pulse_valid(uint16_t pulse_us, uint16_t period_us)
{
    if (pulse_us == 0U) {
        return 1;
    }
    return pulse_us >= 400U && pulse_us <= 2100U && pulse_us < period_us;
}

uint8_t rtl8735b_rc_can_emit(const RTL8735BRCChannelPolicy *channel)
{
    return channel != 0 && channel->enabled && channel->has_valid_write &&
        channel->mode_supported && !channel->safety_disarmed && channel->pulse_us != 0U;
}

uint8_t rtl8735b_rc_brushed_rate_valid(uint16_t rate_hz)
{
    return rate_hz >= RTL8735B_RC_BRUSHED_MIN_HZ && rate_hz <= RTL8735B_RC_BRUSHED_MAX_HZ;
}

uint16_t rtl8735b_rc_brushed_period_ticks(uint16_t rate_hz)
{
    if (!rtl8735b_rc_brushed_rate_valid(rate_hz)) {
        return 0;
    }
    const uint32_t ticks = (RTL8735B_PWM_SCLK_HZ + rate_hz / 2U) / rate_hz;
    return ticks <= RTL8735B_PWM_MAX_TICKS ? (uint16_t)ticks : 0U;
}

uint16_t rtl8735b_rc_brushed_duty_ticks(uint16_t pulse_us, uint16_t esc_min_us,
                                         uint16_t esc_max_us, uint16_t period_ticks)
{
    if (period_ticks == 0U || period_ticks > RTL8735B_PWM_MAX_TICKS || esc_max_us <= esc_min_us ||
        pulse_us <= esc_min_us) {
        return 0;
    }
    // 全開取 period_ticks - 1：DUTY 等於週期時的硬體行為沒有文件，不使用這個邊界值。
    const uint16_t max_duty = (uint16_t)(period_ticks - 1U);
    if (pulse_us >= esc_max_us) {
        return max_duty;
    }
    const uint32_t duty = ((uint32_t)period_ticks * (uint32_t)(pulse_us - esc_min_us)) /
                          (uint32_t)(esc_max_us - esc_min_us);
    return duty > max_duty ? max_duty : (uint16_t)duty;
}

uint16_t rtl8735b_rc_oneshot125_duty_ticks(uint16_t pulse_us)
{
    // 1000–2000 us 對應 125–250 us（除以 8），4 MHz 每 tick 0.25 us：tick = pulse_us / 8 * 4。
    if (pulse_us < 400U || pulse_us > 2100U) {
        return 0;
    }
    return (uint16_t)((uint32_t)pulse_us * (RTL8735B_ONESHOT125_SCLK_HZ / 1000000UL) / 8U);
}
