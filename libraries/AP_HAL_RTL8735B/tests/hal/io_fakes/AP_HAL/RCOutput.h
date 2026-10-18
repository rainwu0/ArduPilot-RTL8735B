#pragma once
#include <stdint.h>
namespace AP_HAL {
class RCOutput {
public:
    virtual ~RCOutput() = default;
    virtual void init() = 0;
    virtual void set_freq(uint32_t chmask, uint16_t freq_hz) = 0;
    virtual uint16_t get_freq(uint8_t chan) = 0;
    virtual void enable_ch(uint8_t chan) = 0;
    virtual void disable_ch(uint8_t chan) = 0;
    virtual void write(uint8_t chan, uint16_t pulse_us) = 0;
    virtual uint16_t read(uint8_t chan) = 0;
    virtual void read(uint16_t *pulse_us, uint8_t len) = 0;
    virtual void cork() = 0;
    virtual void push() = 0;
    enum output_mode {
        MODE_PWM_NONE,
        MODE_PWM_NORMAL,
        MODE_PWM_ONESHOT,
        MODE_PWM_ONESHOT125,
        MODE_PWM_BRUSHED,
        MODE_PWM_DSHOT150,
        MODE_PWM_DSHOT300,
        MODE_PWM_DSHOT600,
        MODE_PWM_DSHOT1200,
        MODE_NEOPIXEL,
        MODE_PROFILED,
        MODE_NEOPIXELRGB,
    };
    enum DshotEscType {
        DSHOT_ESC_NONE = 0,
        DSHOT_ESC_BLHELI = 1,
        DSHOT_ESC_BLHELI_S = 2,
        DSHOT_ESC_BLHELI_EDT = 3,
        DSHOT_ESC_BLHELI_EDT_S = 4
    };
    const static uint32_t ALL_CHANNELS = 255;
    virtual void set_telem_request_mask(uint32_t mask) { (void)mask; }
    virtual void set_dshot_rate(uint8_t dshot_rate, uint16_t loop_rate_hz) { (void)dshot_rate; (void)loop_rate_hz; }
    virtual void set_dshot_esc_type(DshotEscType esc_type) { (void)esc_type; }
    virtual DshotEscType get_dshot_esc_type() const { return DSHOT_ESC_NONE; }
    virtual void set_output_mode(uint32_t mask, enum output_mode mode) = 0;
    virtual enum output_mode get_output_mode(uint32_t &mask) = 0;
    virtual void set_default_rate(uint16_t rate_hz) = 0;
    virtual bool force_safety_on(void) = 0;
    virtual void force_safety_off(void) = 0;
    void set_esc_scaling(uint16_t min_pwm, uint16_t max_pwm) {
        _esc_pwm_min = min_pwm;
        _esc_pwm_max = max_pwm;
    }
protected:
    uint16_t _esc_pwm_min = 1000;
    uint16_t _esc_pwm_max = 2000;
};
}
