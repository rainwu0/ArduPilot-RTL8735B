/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Wi-Fi 韌體更新（OTA）與開機試開檢查的開發板應用程式，見 ota_app.h。
 * 入口直接實作 rtl8735b_app_*；映像以公鑰簽章驗證（ota_core.h）。寫入規則見 ota_core.h，連線協定見 ota_session.h。
 *
 * - Flash 的讀寫在 ota_flash.c（讀取用實體位址，不受 XIP 對映影響）。
 * - 分割表要和編進韌體的版面（建置時給應用程式的編譯定義 RTL8735B_FLASH_FW1_ADDR 等）完全相同，
 *   槽也要放得進 Flash 的實際容量（JEDEC ID 讀不到時假設 OTA_FLASH_ASSUMED_SIZE）；不符時接收端拒絕所有更新。
 *   開機時怎麼處理試開由 ota_boot_plan() 決定。
 * - 開機檢查在 rtl8735b_app_init()：排程器還沒啟動、ArduPilot 的 task 還沒執行，Flash 讀寫不必上鎖、可以重置。
 * - 試開中的映像讓一般解鎖被拒（rtl8735b_app_arming_blocked()）；超過 OTA_TRIAL_TIMEOUT_MS 仍未確認就
 *   重開，解鎖中不重開（ota_trial_timeout_due()）。計時用 FreeRTOS 軟體計時器：主迴圈或其他 task 卡住時仍會執行；
 *   關中斷卡住、HardFault 或排程器停住時不涵蓋。回呼不做會阻塞的事（不拿鎖、不 printf、不呼叫
 *   rtl8735b_ardupilot_outputs_safe()）。試開次數用完但另一槽無效時不計時：重開只會回到同一個映像。
 * - 目前開機的槽由 hal_sys_get_ld_fw_idx() 回報（1 或 2）。
 * - Wi-Fi 是否連上問 SDK 的 wifi_is_connected_to_ap()：應用程式只用 SDK 與 rtl8735b_app.h。
 */

#include "ota_app.h"
#include "ota_core.h"
#include "ota_flash.h"
#include "ota_net.h"
#include "ota_session.h"
#include "rtl8735b_app.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include <objects.h>
#include <hal_sys_ctrl.h>
#include <hal_timer.h>
#include <sys_api.h>
#include <wifi_conf.h>

#if !defined(RTL8735B_FLASH_LAYOUT_NAME) || !defined(RTL8735B_FLASH_FW1_ADDR) || !defined(RTL8735B_FLASH_FW1_LEN) || \
    !defined(RTL8735B_FLASH_FW2_ADDR) || !defined(RTL8735B_FLASH_FW2_LEN)
#error "the board application needs the flash layout definitions (app/rtl8735b_app.h)"
#endif
#ifndef RTL8735B_OTA_PORT
#define RTL8735B_OTA_PORT OTA_DEFAULT_PORT
#endif
#if RTL8735B_OTA_PORT <= 0 || RTL8735B_OTA_PORT > 65535
#error "RTL8735B_OTA_PORT must be 1-65535"
#endif

#define OTA_WAIT_MS             10u
#define OTA_SEND_TIMEOUT_MS     5000u
#define OTA_ACCEPT_POLL_MS      200u
#define OTA_REBOOT_DELAY_MS     500u
// 裝好映像後，等電腦讀到最後的狀態並關閉連線，最多這麼久才重開
#define OTA_PEER_CLOSE_WAIT_MS  10000u
#define OTA_CONFIRM_RETRY_MS    10000u
#define OTA_TRIAL_TICK_MS       1000u
// 主迴圈要在這麼多毫秒內跑過一圈，試開中的映像才算健康
#define OTA_MAIN_LOOP_MAX_MS    500u
// 低於 ArduPilot 的所有執行緒
#define OTA_TASK_PRIORITY       (tskIDLE_PRIORITY + 1)
#define OTA_SERVER_STACK_WORDS  (4096u / sizeof(StackType_t))
#define OTA_CONFIRM_STACK_WORDS (4096u / sizeof(StackType_t))

typedef struct {
    int fd;
} ota_conn_t;

// 含接收緩衝與雜湊狀態，放在靜態區而不是 task 堆疊
static ota_session_ctx_t ota_ctx;
static volatile uint8_t ota_started;
// 接收端曾經成功 listen：試開中的映像要能再收下一次更新才確認
static volatile uint8_t ota_listening;
// 編進韌體的版面加上 Flash 的實際容量；開機檢查時填好
static ota_layout_t ota_layout;
// 試開中、還沒確認：一般解鎖被拒
static volatile uint8_t ota_trial_unconfirmed;
static ota_trial_timer_t ota_trial_state;
static TimerHandle_t ota_trial_timer;

static int _running_idx(void)
{
    return (int)hal_sys_get_ld_fw_idx();
}

static int _wifi_associated(void)
{
    return wifi_is_connected_to_ap() == RTW_SUCCESS;
}

static int _have_key(void)
{
    return rtl8735b_ota_public_key_len == OTA_PUBLIC_KEY_LEN;
}

int rtl8735b_app_arming_blocked(void)
{
    return ota_ctx.in_progress || ota_trial_unconfirmed;
}

static int _main_loop_healthy(void)
{
    return rtl8735b_ardupilot_system_initialized() && rtl8735b_ardupilot_ms_since_main_loop() < OTA_MAIN_LOOP_MAX_MS;
}

// 編進韌體的版面：fw1、fw2 的位址與長度（little-endian、依序相連）；volatile 讓這份資料一定留在映像裡
static const volatile uint32_t ota_layout_words[4] = {
    RTL8735B_FLASH_FW1_ADDR, RTL8735B_FLASH_FW1_LEN, RTL8735B_FLASH_FW2_ADDR, RTL8735B_FLASH_FW2_LEN,
};

// 開機檢查在排程器啟動前讀 JEDEC ID；讀不到時用建置時驗證過的容量，否則版面相同也會被當成不符
static void _layout_init(void)
{
    const uint32_t measured = rtl8735b_ota_flash_size();
    ota_layout.fw[0].addr = ota_layout_words[0];
    ota_layout.fw[0].len = ota_layout_words[1];
    ota_layout.fw[1].addr = ota_layout_words[2];
    ota_layout.fw[1].len = ota_layout_words[3];
    ota_layout.flash_size = ota_flash_capacity(measured);
    if (measured == 0) {
        printf("OTA: flash size unknown, assuming %lu bytes\r\n", (unsigned long)ota_layout.flash_size);
    }
}

// 試開逾時：每秒檢查一次，到期且未解鎖就重開；下一次開機記一次試開（印 trial boot n of 3）。
// 在計時器 task 執行，不能阻塞：不經 rtl8735b_ardupilot_outputs_safe()（輸出的鎖可能正被卡住的主迴圈拿著）、
// 不 printf。未解鎖時輸出已是上鎖狀態的值；先呼叫 rtl8735b_app_before_reset()（依約定不阻塞），再重置晶片
static void _trial_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    const uint32_t since_boot_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if (!ota_trial_timeout_due(&ota_trial_state, since_boot_ms, !ota_trial_unconfirmed, rtl8735b_ardupilot_armed())) {
        return;
    }
    if (!rtl8735b_ardupilot_armed()) {
        rtl8735b_app_before_reset();
        sys_reset();
    }
}

// 試開中的映像在 ota_confirm_ready() 成立時確認：能再收下一次 OTA（接收端已 listen、有公鑰）、
// 主迴圈在跑、Wi-Fi 連上。做不到的映像不確認，試開次數用完後退回舊槽。確認失敗時稍後重試
static void _confirm_task(void *arg)
{
    (void)arg;
    const TickType_t start = xTaskGetTickCount();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        const uint32_t since_boot_ms = (uint32_t)((xTaskGetTickCount() - start) * portTICK_PERIOD_MS);
        const int receiver_ready = ota_listening && _have_key();
        if (!ota_confirm_ready(since_boot_ms, receiver_ready, _main_loop_healthy(),
                               receiver_ready && _wifi_associated())) {
            continue;
        }
        ota_slot_t slots[2];
        const int idx = _running_idx();
        const ota_flash_t *f = rtl8735b_ota_flash();
        if (ota_read_slots(f, &ota_layout, slots) == OTA_OK && ota_confirm(f, slots, idx) == OTA_OK) {
            printf("OTA: fw%d confirmed\r\n", idx);
            ota_trial_unconfirmed = 0;
            if (ota_trial_timer != NULL) {
                xTimerStop(ota_trial_timer, 0);
            }
            vTaskDelete(NULL);
        }
        printf("OTA: fw%d confirm failed, retrying\r\n", idx);
        vTaskDelay(pdMS_TO_TICKS(OTA_CONFIRM_RETRY_MS));
    }
}

void rtl8735b_app_init(void)
{
    ota_slot_t slots[2];
    uint32_t attempts = 0;
    const int idx = _running_idx();
    int action = OTA_BOOT_ERROR;
    const ota_flash_t *f = rtl8735b_ota_flash();
    int pending = 0;
    rtl8735b_ota_flash_use_lock(0);
    _layout_init();
    const int r = ota_read_slots(f, &ota_layout, slots);
    const int plan = ota_boot_plan(r, slots, ota_layout.flash_size);
    if (plan == OTA_PLAN_CHECK || plan == OTA_PLAN_CHECK_NO_CONFIRM) {
        action = ota_boot_check(f, slots, idx, &attempts);
        if (action == OTA_BOOT_ERROR) {
            // 記不了試開次數：目前的槽若還在試開，就是沒確認的映像，照樣擋解鎖
            pending = ota_trial_pending(f, slots, idx) == 1;
        }
    } else if (plan == OTA_PLAN_BLOCK_ONLY) {
        pending = ota_trial_pending(f, slots, idx) == 1;
    }
    rtl8735b_ota_flash_use_lock(1);
    if (r == OTA_ERR_LAYOUT) {
        // 開機不受影響，只是不能用 OTA 更新：要以手動燒錄換成同版面的韌體
        printf("OTA: partition table does not match flash layout %s (flash %lu bytes), updates disabled\r\n",
               RTL8735B_FLASH_LAYOUT_NAME, (unsigned long)ota_layout.flash_size);
    }
    if (plan == OTA_PLAN_BLOCK_ONLY) {
        if (pending) {
            ota_trial_unconfirmed = 1;
            printf("OTA: fw%d is on trial but cannot be confirmed, arming blocked\r\n", idx);
        }
        return;
    }
    switch (action) {
    case OTA_BOOT_TRIAL:
    case OTA_BOOT_NO_FALLBACK:
        ota_trial_unconfirmed = 1;
        if (action == OTA_BOOT_TRIAL) {
            printf("OTA: fw%d trial boot %lu of %u\r\n", idx, (unsigned long)attempts, OTA_TRIAL_MAX_ATTEMPTS);
            ota_trial_timer = xTimerCreate("ota_trial", pdMS_TO_TICKS(OTA_TRIAL_TICK_MS), pdTRUE, NULL, _trial_timer_cb);
            if (ota_trial_timer == NULL || xTimerStart(ota_trial_timer, 0) != pdPASS) {
                printf("OTA: trial timer allocation failed\r\n");
            }
        } else {
            printf("OTA: fw%d used all trial boots but the other slot is not valid\r\n", idx);
        }
        if (plan == OTA_PLAN_CHECK_NO_CONFIRM) {
            printf("OTA: fw%d will not be confirmed with this partition table\r\n", idx);
        } else if (xTaskCreate(_confirm_task, "ota_confirm", OTA_CONFIRM_STACK_WORDS, NULL, OTA_TASK_PRIORITY,
                               NULL) != pdPASS) {
            printf("OTA: confirm task allocation failed\r\n");
        }
        break;
    case OTA_BOOT_ROLLBACK:
        // 排程器還沒啟動，ArduPilot 一行都還沒跑，不必先讓輸出安全
        printf("OTA: fw%d not confirmed after %lu trial boots, falling back\r\n", idx, (unsigned long)attempts);
        sys_reset();
        for (;;) {
        }
    case OTA_BOOT_ERROR:
        printf("OTA: trial check failed\r\n");
        if (pending) {
            ota_trial_unconfirmed = 1;
            printf("OTA: fw%d is on trial but cannot be confirmed, arming blocked\r\n", idx);
        }
        break;
    default:
        break;
    }
}

static int _io_recv(void *ctx, uint8_t *buf, uint32_t len)
{
    const ota_conn_t *c = (const ota_conn_t *)ctx;
    return ota_tcp_recv(c->fd, buf, (uint16_t)(len < 0xFFFFu ? len : 0xFFFFu));
}

static int _io_send(void *ctx, const uint8_t *buf, uint32_t len)
{
    const ota_conn_t *c = (const ota_conn_t *)ctx;
    const TickType_t start = xTaskGetTickCount();
    uint32_t sent = 0;
    while (sent < len) {
        const uint32_t left = len - sent;
        const int n = ota_tcp_send(c->fd, buf + sent, (uint16_t)(left < 0xFFFFu ? left : 0xFFFFu));
        if (n < 0) {
            return -1;
        }
        sent += (uint32_t)n;
        if (n == 0) {
            if (xTaskGetTickCount() - start > pdMS_TO_TICKS(OTA_SEND_TIMEOUT_MS)) {
                return -1;
            }
            vTaskDelay(pdMS_TO_TICKS(OTA_WAIT_MS));
        }
    }
    return 0;
}

static uint64_t _io_now_us(void *ctx)
{
    (void)ctx;
    return hal_read_systime_us();
}

static void _io_wait(void *ctx)
{
    (void)ctx;
    vTaskDelay(pdMS_TO_TICKS(OTA_WAIT_MS));
}

static int _io_armed(void *ctx)
{
    (void)ctx;
    return rtl8735b_ardupilot_armed();
}

// 診斷：每 5% 在主控台印一行，卡住時看得出停在哪裡、當時抹除多久、主迴圈怎樣。
// 抹除包一層計時：真正的抹除在 SDK 的 ROM 裡，抹除時等多久從這裡量得到
static const ota_flash_t *ota_flash_real;
static ota_flash_t ota_flash_timed;
static uint32_t ota_erase_last_us;
static uint32_t ota_erase_max_us;

static int _timed_erase(void *ctx, uint32_t addr)
{
    const uint64_t t0 = hal_read_systime_us();
    const int r = ota_flash_real->erase_sector(ctx, addr);
    const uint32_t dt = (uint32_t)(hal_read_systime_us() - t0);
    ota_erase_last_us = dt;
    if (dt > ota_erase_max_us) {
        ota_erase_max_us = dt;
    }
    return r;
}

static void _io_progress(void *ctx, uint32_t received, uint32_t total)
{
    (void)ctx;
    const unsigned pct = (unsigned)(((uint64_t)received * 100u) / total);
    const unsigned long addr = (unsigned long)(ota_ctx.s.target.addr + received);
    printf("OTA: %3u%% at 0x%08lx erase_us %lu max %lu ml_gap_ms %lu\r\n",
           pct, addr, (unsigned long)ota_erase_last_us, (unsigned long)ota_erase_max_us,
           (unsigned long)rtl8735b_ardupilot_ms_since_main_loop());
}

// 最後的狀態可能還在送出緩衝或在 Wi-Fi 上掉了要重送；這時就重置，電腦收不到狀態，
// 只能等逾時、回報 installation state unknown。電腦讀到狀態後會關閉連線，所以等到對方關閉
// （或連線出錯、逾時）再重開。收到的資料丟掉
static void _wait_peer_close(int fd)
{
    uint8_t scratch[16];
    const TickType_t start = xTaskGetTickCount();
    while (xTaskGetTickCount() - start < pdMS_TO_TICKS(OTA_PEER_CLOSE_WAIT_MS)) {
        if (ota_tcp_recv(fd, scratch, sizeof(scratch)) < 0) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(OTA_WAIT_MS));
    }
}

static void _server_task(void *arg)
{
    (void)arg;
    int listen_fd = -1;
    for (;;) {
        if (listen_fd < 0) {
            if (!_wifi_associated() || (listen_fd = ota_tcp_listen(RTL8735B_OTA_PORT)) < 0) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            printf("OTA: listening on TCP %u\r\n", (unsigned)RTL8735B_OTA_PORT);
            ota_listening = 1;
        }
        uint32_t address;
        uint16_t port;
        const int fd = ota_tcp_accept(listen_fd, &address, &port);
        if (fd < 0) {
            // 沒有等待中的連線就繼續等；其他錯誤（例如 Wi-Fi 重連後 socket 失效）重新 listen
            if (fd != OTA_NET_NONE) {
                ota_tcp_close(listen_fd);
                listen_fd = -1;
            }
            vTaskDelay(pdMS_TO_TICKS(OTA_ACCEPT_POLL_MS));
            continue;
        }
        // 一次只服務一條連線；其他連線在 listen 的佇列（長度 1）等，或被拒絕
        ota_conn_t conn = {fd};
        const ota_io_t io = {_io_recv, _io_send, _io_now_us, _io_wait, _io_armed, &conn, _io_progress};
        ota_erase_last_us = 0;
        ota_erase_max_us = 0;
        ota_ctx.io = &io;
        ota_ctx.running_idx = _running_idx();
        const int r = ota_session_serve(&ota_ctx);
        if (ota_ctx.reboot) {
            _wait_peer_close(fd);
        }
        ota_tcp_close(fd);
        printf("OTA: %lu.%lu.%lu.%lu: %s (%d)\r\n", (unsigned long)(address >> 24), (unsigned long)((address >> 16) & 0xFF),
               (unsigned long)((address >> 8) & 0xFF), (unsigned long)(address & 0xFF),
               ota_ctx.probe ? "trial probe answered" : r == OTA_OK ? "image installed" : "refused or failed", r);
        // 要重開時「OTA 進行中」旗標還在，一般解鎖一直被擋到重置；先讓狀態送出，重置前再查一次解鎖。
        // 重置前先呼叫 rtl8735b_app_before_reset()，再讓輸出安全
        if (ota_ctx.reboot) {
            vTaskDelay(pdMS_TO_TICKS(OTA_REBOOT_DELAY_MS));
            if (!rtl8735b_ardupilot_armed()) {
                printf("OTA: rebooting\r\n");
                rtl8735b_app_before_reset();
                rtl8735b_ardupilot_outputs_safe();
                sys_reset();
            }
            printf("OTA: armed, reboot skipped\r\n");
            ota_ctx.in_progress = 0;
        }
    }
}

void rtl8735b_app_after_setup(void)
{
    if (ota_started) {
        return;
    }
    ota_started = 1;
    ota_flash_real = rtl8735b_ota_flash();
    ota_flash_timed = *ota_flash_real;
    ota_flash_timed.erase_sector = _timed_erase;
    ota_ctx.flash = &ota_flash_timed;
    ota_ctx.layout = &ota_layout;
    ota_ctx.public_key = rtl8735b_ota_public_key;
    ota_ctx.public_key_len = rtl8735b_ota_public_key_len;
    if (!_have_key()) {
        printf("OTA: no public key in this build, every update will be refused\r\n");
    }
    if (xTaskCreate(_server_task, "ota", OTA_SERVER_STACK_WORDS, NULL, OTA_TASK_PRIORITY, NULL) != pdPASS) {
        printf("OTA: server task allocation failed\r\n");
    }
}
