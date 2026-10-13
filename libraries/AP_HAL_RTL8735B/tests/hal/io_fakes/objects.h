#pragma once
// 測試替身。出處：名稱與數值依 SDK d1b6426 的 component/mbed/targets/hal/rtl8735b/objects.h（Apache-2.0）；只寫受測程式用到的部分，自己撰寫。
// PWM_TypeDef 只列 io_shim.c 用到的暫存器名稱，取自 SDK d1b6426 的 component/soc/8735b/fwlib/rtl8735b/include/rtl8735b_pwm_type.h；該檔沒有授權標示。
#include <stdint.h>

typedef enum { PullNone = 0, PullUp, PullDown, OpenDrain } PinMode;
typedef enum { PIN_INPUT = 0, PIN_OUTPUT = 1 } PinDirection;

typedef struct {
    uint32_t pin;
    int direction;
    int value;
} gpio_t;

typedef enum { IRQ_NONE = 0, IRQ_RISE = 1, IRQ_FALL = 2 } gpio_irq_event;
typedef void (*gpio_irq_handler)(uint32_t id, gpio_irq_event event);
typedef struct {
    uint32_t pin;
    uint32_t id;
    gpio_irq_handler handler;
    gpio_irq_event event;
    int enabled;
} gpio_irq_t;

// SDK 的 PWM 暫存器只模擬 PWM_CTRL（CTRL_SET、DUTY）與 PWM_TIMING_CTRL（DUTY_START、PERIOD）。
typedef struct {
    volatile uint32_t PWM_CTRL;
    volatile uint32_t PWM_TIMING_CTRL;
} PWM_TypeDef;

typedef struct {
    int channel;
    PWM_TypeDef *base_addr;
} hal_pwm_adapter_t;

typedef struct {
    uint8_t pwm_idx;
    uint8_t pin_sel;
    uint8_t is_init;
    uint32_t period;
    uint32_t pulse;
    hal_pwm_adapter_t pwm_hal_adp;
} pwmout_t;

typedef struct {
    struct { struct { uint32_t w; } pin_en; } plft_dat;
} hal_adc_adapter_t;

typedef int hal_status_t;
