#pragma once
#include <stdint.h>
#include <stdio.h>
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
    virtual void set_reversible_mask(uint32_t chanmask) { (void)chanmask; }
    virtual void set_dshot_rate(uint8_t dshot_rate, uint16_t loop_rate_hz) { (void)dshot_rate; (void)loop_rate_hz; }
    virtual void set_dshot_esc_type(DshotEscType esc_type) { (void)esc_type; }
    virtual DshotEscType get_dshot_esc_type() const { return DSHOT_ESC_NONE; }
    virtual void send_dshot_command(uint8_t command, uint8_t chan = ALL_CHANNELS, uint32_t command_timeout_ms = 0,
                                    uint16_t repeat_count = 10, bool priority = false)
    {
        (void)command; (void)chan; (void)command_timeout_ms; (void)repeat_count; (void)priority;
    }
    virtual bool get_output_mode_banner(char banner_msg[], uint8_t banner_msg_len) const
    {
        (void)banner_msg; (void)banner_msg_len;
        return false;
    }
    // 照 ArduPilot 的 AP_HAL/RCOutput.cpp：模式名稱與「 名稱:起-迄」的接法
    const char *get_output_mode_string(enum output_mode out_mode) const
    {
        switch (out_mode) {
        case MODE_PWM_NONE: return "None";
        case MODE_PWM_NORMAL: return "PWM";
        case MODE_PWM_ONESHOT: return "OneS";
        case MODE_PWM_ONESHOT125: return "OS125";
        case MODE_PWM_BRUSHED: return "Brush";
        case MODE_PWM_DSHOT150: return "DS150";
        case MODE_PWM_DSHOT300: return "DS300";
        case MODE_PWM_DSHOT600: return "DS600";
        case MODE_PWM_DSHOT1200: return "DS1200";
        default: return "Unknown";
        }
    }
    void append_to_banner(char banner_msg[], uint8_t banner_msg_len, output_mode out_mode, uint8_t low_ch,
                          uint8_t high_ch) const
    {
        char temp[256];
        snprintf(temp, sizeof(temp), "%s", banner_msg);
        if (low_ch == high_ch) {
            snprintf(banner_msg, banner_msg_len, "%s %s:%u", temp, get_output_mode_string(out_mode), (unsigned)low_ch);
        } else {
            snprintf(banner_msg, banner_msg_len, "%s %s:%u-%u", temp, get_output_mode_string(out_mode),
                     (unsigned)low_ch, (unsigned)high_ch);
        }
    }
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
