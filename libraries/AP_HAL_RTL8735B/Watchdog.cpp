/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "Watchdog.h"

#include <stdio.h>

#include "wdt_shim.h"

using namespace RTL8735B;

void Watchdog::start(bool enabled)
{
    if (!enabled || _started) {
        return;
    }
    if (rtl8735b_watchdog_start(TIMEOUT_MS) == 0) {
        printf("RTL8735B: hardware watchdog enabled (%u ms)\n", (unsigned)TIMEOUT_MS);
    } else {
        // 命令已下，但 VNDR 看門狗的致能位元沒有讀回 1。仍照常餵狗：若實際在計時，不餵會被重置。
        printf("RTL8735B: hardware watchdog start not confirmed\n");
    }
    _started = true;
}

void Watchdog::pat(uint32_t now_ms)
{
    if (_started) {
        rtl8735b_watchdog_refresh();
    }
    _last_pat_ms = now_ms;
}

void Watchdog::expect_delay(uint32_t now_ms, uint32_t ms)
{
    if (ms == 0) {
        if (_expect_delay_nesting > 0) {
            _expect_delay_nesting--;
        }
        if (_expect_delay_nesting == 0) {
            _expect_delay_start = 0;
        }
        return;
    }
    if (_expect_delay_start != 0) {
        // 已經有預期延遲在進行：取剩餘時間與新要求的較長者
        const uint32_t done = now_ms - _expect_delay_start;
        if (_expect_delay_length > done && _expect_delay_length - done > ms) {
            ms = _expect_delay_length - done;
        }
    }
    _expect_delay_start = now_ms;
    _expect_delay_length = ms;
    _expect_delay_nesting++;
}

bool Watchdog::in_expected_delay(uint32_t now_ms) const
{
    const uint32_t start = _expect_delay_start;
    return start != 0 && now_ms - start <= _expect_delay_length;
}
