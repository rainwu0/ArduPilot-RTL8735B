#include "io_fakes/hal_pwm.h"
#include "io_fakes/io_shim_sdk_fake.h"
#include "io_fakes/objects.h"
#include "io_fakes/PinNames.h"

#include <string.h>

int io_fake_adc_init_status;
int io_fake_adc_cvlist_status;
uint16_t io_fake_adc_value;
unsigned io_fake_adc_init_calls;
uint32_t io_fake_adc_pin_en;
uint8_t io_fake_adc_cvlist_channel = 0xFF;
uint8_t io_fake_adc_read_channel = 0xFF;
unsigned io_fake_gpio_init_calls;
unsigned io_fake_pwm_init_calls;
unsigned io_fake_pwm_start_calls;
unsigned io_fake_pwm_stop_calls;
unsigned io_fake_pwm_period_calls;
unsigned io_fake_pwm_pulse_calls;
unsigned io_fake_gpio_write_calls;
unsigned io_fake_gpio_dir_calls;
unsigned io_fake_gpio_irq_deinit_calls;
unsigned io_fake_gpio_irq_init_calls;
unsigned io_fake_last_gpio_order_write;
unsigned io_fake_last_gpio_order_dir;
unsigned io_fake_order_pwm_init;
unsigned io_fake_order_pwm_stop;
unsigned io_fake_order_pwm_period;
unsigned io_fake_order_pwm_pulse;
unsigned io_fake_order_pwm_start;
int io_fake_pwm_running[12];
uint16_t io_fake_pwm_period[12];
uint16_t io_fake_pwm_pulse[12];
unsigned io_fake_pwm_enable_calls;
unsigned io_fake_pwm_disable_calls;
unsigned io_fake_pwm_duty_ns_calls;
unsigned io_fake_order_pwm_duty_ns;
unsigned io_fake_order_pwm_enable;
unsigned io_fake_order_pwm_disable;
int io_fake_pwm_duty_ns_status;
uint32_t io_fake_pwm_period_ns[12];
uint32_t io_fake_pwm_duty_ns[12];
uint32_t io_fake_pwm_offset_ns[12];
unsigned io_fake_pwm_duty_us_calls;
unsigned io_fake_pwm_duty_us_waits;
unsigned io_fake_order_pwm_duty_us;
int io_fake_pwm_duty_us_status;
unsigned io_fake_pwm_sclk_sel_calls;
int io_fake_pwm_sclk;
unsigned io_fake_order_pwm_sclk_sel;
uint8_t io_fake_pwm_clk_sel[4];
unsigned io_fake_pwm_wait_ready_calls;
unsigned io_fake_pwm_update_ctrl_calls;
uint32_t io_fake_timestamp_us;
int io_fake_gpio_irq_init_status;
void (*io_fake_pwm_duty_ns_preempt)(void);

namespace {
unsigned operation_order;
gpio_irq_t *irq_objects[3];
PWM_TypeDef pwm_registers[4];
int pwm_index(uint32_t pin)
{
    switch (pin) {
    case PIN_F14: return 0;
    case PIN_F11: return 1;
    case PIN_F12: return 2;
    case PIN_F13: return 3;
    default: return -1;
    }
}
}

extern "C" void io_fake_reset(void)
{
    io_fake_adc_init_status = 0;
    io_fake_adc_cvlist_status = 0;
    io_fake_adc_value = 0;
    io_fake_adc_init_calls = 0;
    io_fake_adc_pin_en = 0;
    io_fake_adc_cvlist_channel = 0xFF;
    io_fake_adc_read_channel = 0xFF;
    io_fake_gpio_init_calls = 0;
    io_fake_pwm_init_calls = 0;
    io_fake_pwm_start_calls = 0;
    io_fake_pwm_stop_calls = 0;
    io_fake_pwm_period_calls = 0;
    io_fake_pwm_pulse_calls = 0;
    io_fake_gpio_write_calls = 0;
    io_fake_gpio_dir_calls = 0;
    io_fake_gpio_irq_deinit_calls = 0;
    io_fake_gpio_irq_init_calls = 0;
    io_fake_last_gpio_order_write = 0;
    io_fake_last_gpio_order_dir = 0;
    io_fake_order_pwm_init = 0;
    io_fake_order_pwm_stop = 0;
    io_fake_order_pwm_period = 0;
    io_fake_order_pwm_pulse = 0;
    io_fake_order_pwm_start = 0;
    io_fake_pwm_enable_calls = 0;
    io_fake_pwm_disable_calls = 0;
    io_fake_pwm_duty_ns_calls = 0;
    io_fake_order_pwm_duty_ns = 0;
    io_fake_order_pwm_enable = 0;
    io_fake_order_pwm_disable = 0;
    io_fake_pwm_duty_ns_status = 0;
    memset(io_fake_pwm_period_ns, 0, sizeof(io_fake_pwm_period_ns));
    memset(io_fake_pwm_duty_ns, 0, sizeof(io_fake_pwm_duty_ns));
    memset(io_fake_pwm_offset_ns, 0, sizeof(io_fake_pwm_offset_ns));
    io_fake_pwm_duty_us_calls = 0;
    io_fake_pwm_duty_us_waits = 0;
    io_fake_order_pwm_duty_us = 0;
    io_fake_pwm_duty_us_status = 0;
    io_fake_pwm_sclk_sel_calls = 0;
    io_fake_pwm_sclk = 0;
    io_fake_order_pwm_sclk_sel = 0;
    memset(io_fake_pwm_clk_sel, 0, sizeof(io_fake_pwm_clk_sel));
    io_fake_pwm_wait_ready_calls = 0;
    io_fake_pwm_update_ctrl_calls = 0;
    io_fake_timestamp_us = 12345;
    io_fake_gpio_irq_init_status = 0;
    io_fake_pwm_duty_ns_preempt = nullptr;
    operation_order = 0;
    memset(io_fake_pwm_running, 0, sizeof(io_fake_pwm_running));
    memset(io_fake_pwm_period, 0, sizeof(io_fake_pwm_period));
    memset(io_fake_pwm_pulse, 0, sizeof(io_fake_pwm_pulse));
    memset(irq_objects, 0, sizeof(irq_objects));
    // 暫存器狀態屬於硬體，不隨替身計數重設（io_shim 的執行狀態也不重設）。
}

extern "C" void io_fake_pwm_period_end(int index)
{
    if (index >= 0 && index < 4 && io_fake_pwm_running[index]) {
        pwm_registers[index].PWM_CTRL &= ~PWM_BIT_CTRL_SET;
    }
}

extern "C" PWM_TypeDef *io_fake_pwm_regs(int index)
{
    return &pwm_registers[index];
}

extern "C" void hal_pwm_clk_sel(int sclk)
{
    io_fake_pwm_sclk_sel_calls++;
    io_fake_order_pwm_sclk_sel = ++operation_order;
    io_fake_pwm_sclk = sclk;
}

extern "C" void hal_pwm_set_clk_sel(hal_pwm_adapter_t *adp, pwm_clk_sel_t clk_sel)
{
    if (adp->channel >= 0 && adp->channel < 4) {
        io_fake_pwm_clk_sel[adp->channel] = clk_sel;
    }
}

// 等到硬體套用上一次的設定（這裡視為週期已結束）。
extern "C" void hal_pwm_wait_ctrl_ready(hal_pwm_adapter_t *adp)
{
    io_fake_pwm_wait_ready_calls++;
    adp->base_addr->PWM_CTRL &= ~PWM_BIT_CTRL_SET;
}

extern "C" void io_fake_fire_irq(uint32_t id, int event)
{
    if (id < 3 && irq_objects[id] != nullptr && irq_objects[id]->enabled &&
        irq_objects[id]->handler != nullptr) {
        irq_objects[id]->handler(irq_objects[id]->id, (gpio_irq_event)event);
    }
}

extern "C" void gpio_init(gpio_t *obj, uint32_t pin)
{
    io_fake_gpio_init_calls++;
    obj->pin = pin;
    obj->direction = PIN_INPUT;
    obj->value = 0;
}

extern "C" void gpio_mode(gpio_t *, PinMode) {}

extern "C" void gpio_dir(gpio_t *obj, PinDirection direction)
{
    io_fake_gpio_dir_calls++;
    io_fake_last_gpio_order_dir = ++operation_order;
    obj->direction = direction;
}

extern "C" void gpio_write(gpio_t *obj, int value)
{
    io_fake_gpio_write_calls++;
    io_fake_last_gpio_order_write = ++operation_order;
    obj->value = value != 0;
}

extern "C" int gpio_read(gpio_t *obj)
{
    return obj->value;
}

extern "C" int gpio_irq_init(gpio_irq_t *obj, uint32_t pin, gpio_irq_handler handler, uint32_t id)
{
    io_fake_gpio_irq_init_calls++;
    if (io_fake_gpio_irq_init_status != 0) {
        return io_fake_gpio_irq_init_status;
    }
    obj->pin = pin;
    obj->handler = handler;
    obj->id = id;
    obj->enabled = 0;
    irq_objects[id] = obj;
    return 0;
}

extern "C" void gpio_irq_set(gpio_irq_t *obj, gpio_irq_event event, uint32_t enable)
{
    obj->event = event;
    obj->enabled = enable != 0;
}

extern "C" void gpio_irq_disable(gpio_irq_t *obj)
{
    obj->enabled = 0;
}

extern "C" void gpio_irq_deinit(gpio_irq_t *obj)
{
    io_fake_gpio_irq_deinit_calls++;
    if (obj->id < 3) {
        irq_objects[obj->id] = nullptr;
    }
    obj->enabled = 0;
    obj->handler = nullptr;
}

extern "C" void hal_adc_load_default(hal_adc_adapter_t *obj)
{
    memset(obj, 0, sizeof(*obj));
}

extern "C" hal_status_t hal_adc_init(hal_adc_adapter_t *obj)
{
    io_fake_adc_init_calls++;
    io_fake_adc_pin_en = obj->plft_dat.pin_en.w;
    return io_fake_adc_init_status;
}

// 只接受一個通道（ADC 0–3）；讀取時只有轉換清單裡的那個通道有值。
extern "C" hal_status_t hal_adc_set_cvlist(hal_adc_adapter_t *, uint8_t *channels, uint8_t count)
{
    if (count != 1 || channels[0] > 3) {
        return -20;
    }
    io_fake_adc_cvlist_channel = channels[0];
    return io_fake_adc_cvlist_status;
}

extern "C" void hal_adc_set_in_type_all(hal_adc_adapter_t *, uint32_t) {}
extern "C" hal_status_t hal_adc_deinit(hal_adc_adapter_t *) { return 0; }
extern "C" uint16_t hal_adc_single_read(hal_adc_adapter_t *, uint8_t channel)
{
    io_fake_adc_read_channel = channel;
    return channel == io_fake_adc_cvlist_channel ? io_fake_adc_value : 0;
}
extern "C" void hal_delay_ms(uint32_t) {}

extern "C" void pwmout_init(pwmout_t *obj, uint32_t pin)
{
    const int index = pwm_index(pin);
    obj->pwm_idx = index == 0 ? 8 : (index == 1 ? 5 : (index == 2 ? 6 : 7));
    obj->is_init = 1;
    obj->pwm_hal_adp.channel = index;
    obj->pwm_hal_adp.base_addr = index >= 0 ? &pwm_registers[index] : nullptr;
    if (index >= 0) {
        pwm_registers[index].PWM_CTRL = 0;
    }
    obj->period = 20000;
    obj->pulse = 0;
    io_fake_pwm_init_calls++;
    io_fake_order_pwm_init = ++operation_order;
    if (index >= 0) {
        io_fake_pwm_running[index] = 1; // 模擬 SDK 以預設零脈寬啟動 PWM。
        io_fake_pwm_period[index] = 20000;
        io_fake_pwm_pulse[index] = 0;
    }
}

extern "C" void pwmout_period_us(pwmout_t *obj, int us)
{
    const int index = pwm_index(obj->pwm_idx == 8 ? PIN_F14 :
        (obj->pwm_idx == 5 ? PIN_F11 : (obj->pwm_idx == 6 ? PIN_F12 : PIN_F13)));
    obj->period = (uint32_t)us;
    io_fake_pwm_period_calls++;
    io_fake_order_pwm_period = ++operation_order;
    if (index >= 0) { io_fake_pwm_period[index] = (uint16_t)us; }
}

extern "C" void pwmout_pulsewidth_us(pwmout_t *obj, int us)
{
    int index = obj->pwm_idx == 8 ? 0 : (obj->pwm_idx == 5 ? 1 : (obj->pwm_idx == 6 ? 2 : 3));
    obj->pulse = (uint32_t)us;
    io_fake_pwm_pulse_calls++;
    io_fake_order_pwm_pulse = ++operation_order;
    io_fake_pwm_pulse[index] = (uint16_t)us;
}

extern "C" void pwmout_start(pwmout_t *obj)
{
    const int index = obj->pwm_idx == 8 ? 0 : (obj->pwm_idx == 5 ? 1 : (obj->pwm_idx == 6 ? 2 : 3));
    io_fake_pwm_start_calls++;
    io_fake_order_pwm_start = ++operation_order;
    io_fake_pwm_running[index] = 1;
    // 硬體在啟用時套用設定並清除 CTRL_SET。
    pwm_registers[index].PWM_CTRL &= ~PWM_BIT_CTRL_SET;
}

extern "C" void pwmout_stop(pwmout_t *obj)
{
    const int index = obj->pwm_idx == 8 ? 0 : (obj->pwm_idx == 5 ? 1 : (obj->pwm_idx == 6 ? 2 : 3));
    io_fake_pwm_stop_calls++;
    io_fake_order_pwm_stop = ++operation_order;
    io_fake_pwm_running[index] = 0;
}

// 模擬 ROM 的 hal_pwm_set_duty（推論，與公開原始碼的 hal_pwm_set_duty_ns 同模式）：通道啟用中時
// 先等 CTRL_SET 清除（這裡計數後視為週期已結束），寫入後設 CTRL_SET；停用中直接寫入。
extern "C" hal_status_t hal_pwm_set_duty(hal_pwm_adapter_t *adp, uint32_t period_us,
                                         uint32_t duty_us, uint32_t)
{
    io_fake_pwm_duty_us_calls++;
    io_fake_order_pwm_duty_us = ++operation_order;
    if (io_fake_pwm_duty_us_status != 0) {
        return io_fake_pwm_duty_us_status;
    }
    const int index = adp->channel;
    if (index < 0 || index >= 4) {
        return 0;
    }
    PWM_TypeDef *regs = adp->base_addr;
    if (io_fake_pwm_running[index] && (regs->PWM_CTRL & PWM_BIT_CTRL_SET) != 0U) {
        io_fake_pwm_duty_us_waits++;
    }
    io_fake_pwm_period[index] = (uint16_t)period_us;
    io_fake_pwm_pulse[index] = (uint16_t)duty_us;
    regs->PWM_CTRL = io_fake_pwm_running[index] ? PWM_BIT_CTRL_SET : 0U;
    return 0;
}

// 模擬 SDK hal_pwm_set_duty_ns：以 SCLK 為 tick，週期上限 102375 ns、下限 50 ns。
extern "C" hal_status_t hal_pwm_set_duty_ns(hal_pwm_adapter_t *adp, uint32_t period_ns,
                                            uint32_t duty_ns, uint32_t start_offset_ns)
{
    io_fake_pwm_duty_ns_calls++;
    io_fake_order_pwm_duty_ns = ++operation_order;
    if (duty_ns != 0U && io_fake_pwm_duty_ns_preempt != nullptr) {
        void (*preempt)(void) = io_fake_pwm_duty_ns_preempt;
        io_fake_pwm_duty_ns_preempt = nullptr;
        preempt();
    }
    if (io_fake_pwm_duty_ns_status != 0) {
        return io_fake_pwm_duty_ns_status;
    }
    if (period_ns > 102375U || period_ns < 50U) {
        return 6;
    }
    if (adp->channel >= 0 && adp->channel < 12) {
        io_fake_pwm_period_ns[adp->channel] = period_ns;
        io_fake_pwm_duty_ns[adp->channel] = duty_ns;
        io_fake_pwm_offset_ns[adp->channel] = start_offset_ns;
    }
    return 0;
}

extern "C" void hal_pwm_enable(hal_pwm_adapter_t *adp)
{
    io_fake_pwm_enable_calls++;
    io_fake_order_pwm_enable = ++operation_order;
    if (adp->channel >= 0 && adp->channel < 12) {
        io_fake_pwm_running[adp->channel] = 1;
    }
    // 硬體在啟用時套用設定並清除 CTRL_SET。
    if (adp->base_addr != nullptr) {
        adp->base_addr->PWM_CTRL &= ~PWM_BIT_CTRL_SET;
    }
}

extern "C" void hal_pwm_disable(hal_pwm_adapter_t *adp)
{
    io_fake_pwm_disable_calls++;
    io_fake_order_pwm_disable = ++operation_order;
    if (adp->channel >= 0 && adp->channel < 12) {
        io_fake_pwm_running[adp->channel] = 0;
    }
}

extern "C" uint32_t us_ticker_read(void)
{
    return io_fake_timestamp_us;
}
