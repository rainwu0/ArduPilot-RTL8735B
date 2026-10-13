/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "RCInput.h"
#include <AP_RCProtocol/AP_RCProtocol.h>
#include <assert.h>
#include <atomic>
#include <limits>
#include <stdexcept>
#include <stdio.h>
#include <thread>

static AP_RCProtocol decoder;
namespace AP { AP_RCProtocol &RC() { return decoder; } }
namespace AP_HAL { void panic(const char *, ...) { throw std::runtime_error("panic"); } }

static void rc_contract()
{
    RTL8735B::RCInput input;
    assert(!input.new_input());
    assert(input.num_channels() == 0);
    assert(input.read(0) == 0);
    assert(input.get_rssi() == -1 && input.get_rx_link_quality() == -1);
    input.init();
    input.init();
    assert(decoder.init_calls == 1);
    // 外部解碼呼叫期間，其他 task 必須仍能取得 RC 快照鎖。
    decoder.before_read = [&input] {
        std::thread observer([&input] { (void)input.num_channels(); });
        observer.join();
    };
    input._timer_tick();
    decoder.before_read = nullptr;
    decoder.count = 2;
    decoder.channels[0] = 1100;
    decoder.channels[1] = 1700;
    decoder.rssi = 93;
    decoder.quality = 97;
    decoder.pending = true;
    input._timer_tick();
    assert(input.new_input() && !input.new_input());
    assert(input.num_channels() == 2 && input.read(1) == 1700 && input.read(2) == 0);
    assert(input.get_rssi() == 93 && input.get_rx_link_quality() == 97);
    assert(strcmp(input.protocol(), "CRSF") == 0);
    uint16_t out[4] = {0, 0, 4242, 4242};
    assert(input.read(out, 4) == 2 && out[2] == 4242);
    assert(input.read(nullptr, 4) == 0);
    // 沒有有效 frame 時不能產生虛假的新輸入；共用 RC 層用此條件判斷失聯。
    decoder.pending = false;
    input._timer_tick();
    assert(!input.new_input() && input.read(0) == 1100);
    decoder.count = 0;
    decoder.pending = true;
    input._timer_tick();
    assert(!input.new_input() && input.num_channels() == 2);
    decoder.count = 32;
    decoder.pending = true;
    input._timer_tick();
    assert(input.num_channels() == 18 && input.read(18) == 0);
}

static void rc_snapshot_concurrency()
{
    RTL8735B::RCInput input;
    input.init();
    std::atomic<bool> done{false};
    std::thread producer([&] {
        for (unsigned n = 1; n <= 20000; n++) {
            decoder.count = 18;
            for (auto &channel : decoder.channels) { channel = uint16_t(n); }
            decoder.pending = true;
            input._timer_tick();
        }
        done = true;
    });
    while (!done) {
        uint16_t out[18]{};
        const uint8_t count = input.read(out, 18);
        for (uint8_t i = 1; i < count; i++) { assert(out[i] == out[0]); }
    }
    producer.join();
}

int main()
{
    rc_contract();
    rc_snapshot_concurrency();
    puts("PASS: RC frame/loss/lock-order/coherent snapshot contracts");
}
