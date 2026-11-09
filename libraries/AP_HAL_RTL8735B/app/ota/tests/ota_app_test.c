/*
 * ota_app.c 的主機測試：真的 ota_core.c（試開紀錄、確認、退回），其餘都是替身：
 * 16 MiB 的假 NOR Flash（版面 realtek-ota）、FreeRTOS（task 不會自己跑，測試以 setjmp 逐一驅動）、
 * SDK 的重置與 Wi-Fi 狀態、HAL 的 rtl8735b_ardupilot_*、rtl8735b_app_before_reset()、TCP 與 ota_session_serve()。
 * sys_reset()、vTaskDelete() 與 vTaskDelay() 用完次數時以 longjmp 回到測試。
 * ota_app.c 的 printf 以 -Dprintf=fake_printf 收進 out[]；測試自己的輸出用 fputs。
 * 用法：ota_app_test <案例>，通過時印 ok 並以 0 結束；不帶參數時列出案例。
 */
#include "ota_app.h"
#include "ota_core.h"
#include "ota_flash.h"
#include "ota_net.h"
#include "ota_session.h"
#include "rtl8735b_app.h"

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include <hal_sys_ctrl.h>
#include <hal_timer.h>
#include <sys_api.h>
#include <wifi_conf.h>

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLASH_SIZE 0x1000000u
#define FW1_ADDR 0x60000u
#define FW2_ADDR 0x520000u
#define SLOT_LEN 0x400000u

#define CHECK(c) do { if (!(c)) { fprintf(stdout, "FAIL %s:%d %s\n", __func__, __LINE__, #c); \
    fputs(out, stdout); exit(1); } } while (0)

enum { ESC_NONE = 0, ESC_RESET = 1, ESC_DELETE = 2, ESC_BUDGET = 3 };

// ---- 公鑰（建置產生的 ota_public_key.c 的替身；連線協定是替身，不驗簽章） ----
const uint8_t rtl8735b_ota_public_key[32] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                                             17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
const uint32_t rtl8735b_ota_public_key_len = 32;

// ---- 輸出與事件 ----
static char out[16384];
static size_t out_len;
// 依序記下 B（rtl8735b_app_before_reset）、S（rtl8735b_ardupilot_outputs_safe）、R（sys_reset）
static char events[64];
static int n_events;

int fake_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(out + out_len, sizeof(out) - out_len, fmt, ap);
    va_end(ap);
    if (n > 0) {
        out_len += (size_t)n < sizeof(out) - out_len ? (size_t)n : sizeof(out) - out_len - 1;
    }
    return n;
}

static void event(char c)
{
    if (n_events < (int)sizeof(events) - 1) {
        events[n_events++] = c;
    }
}

// ---- 跳出 task 迴圈 ----
static jmp_buf escape;
static int delay_budget;

static int run_task(TaskFunction_t fn, int budget)
{
    delay_budget = budget;
    const int r = setjmp(escape);
    if (r == ESC_NONE) {
        fn(NULL);
    }
    return r;
}

// ---- FreeRTOS ----
static TickType_t now_ms;
typedef struct { TaskFunction_t fn; const char *name; UBaseType_t priority; } created_t;
static created_t created[8];
static int n_created;
static TimerCallbackFunction_t timer_cb;
static int timer_stopped;

BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack_words, void *arg, UBaseType_t priority,
                       TaskHandle_t *handle)
{
    (void)stack_words; (void)arg; (void)handle;
    if (n_created < 8) {
        created[n_created].fn = fn;
        created[n_created].name = name;
        created[n_created].priority = priority;
        n_created++;
    }
    return pdPASS;
}

void vTaskDelay(TickType_t ticks)
{
    now_ms += ticks;
    if (--delay_budget <= 0) {
        longjmp(escape, ESC_BUDGET);
    }
}

TickType_t xTaskGetTickCount(void)
{
    return now_ms;
}

void vTaskDelete(TaskHandle_t task)
{
    (void)task;
    longjmp(escape, ESC_DELETE);
}

TimerHandle_t xTimerCreate(const char *name, TickType_t period, UBaseType_t auto_reload, void *id,
                           TimerCallbackFunction_t callback)
{
    (void)name; (void)period; (void)auto_reload; (void)id;
    timer_cb = callback;
    return (TimerHandle_t)&timer_cb;
}

BaseType_t xTimerStart(TimerHandle_t timer, TickType_t wait)
{
    (void)timer; (void)wait;
    return pdPASS;
}

BaseType_t xTimerStop(TimerHandle_t timer, TickType_t wait)
{
    (void)timer; (void)wait;
    timer_stopped = 1;
    return pdPASS;
}

static TaskFunction_t task_named(const char *name)
{
    for (int i = 0; i < n_created; i++) {
        if (strcmp(created[i].name, name) == 0) {
            return created[i].fn;
        }
    }
    return NULL;
}

static int count_named(const char *name)
{
    int n = 0;
    for (int i = 0; i < n_created; i++) {
        n += strcmp(created[i].name, name) == 0;
    }
    return n;
}

// ---- SDK ----
static uint32_t running_idx = 1;

uint32_t hal_sys_get_ld_fw_idx(void)
{
    return running_idx;
}

uint64_t hal_read_systime_us(void)
{
    return (uint64_t)now_ms * 1000u;
}

void sys_reset(void)
{
    event('R');
    longjmp(escape, ESC_RESET);
}


// ---- HAL 的 rtl8735b_ardupilot_* 與 SDK 的 Wi-Fi 狀態 ----
static int armed;
static int system_initialized = 1;
static uint32_t ms_since_main_loop = 10;
static int wifi_associated = 1;

int rtl8735b_ardupilot_armed(void) { return armed; }
int rtl8735b_ardupilot_system_initialized(void) { return system_initialized; }
uint32_t rtl8735b_ardupilot_ms_since_main_loop(void) { return ms_since_main_loop; }
int wifi_is_connected_to_ap(void) { return wifi_associated ? RTW_SUCCESS : -1; }
void rtl8735b_ardupilot_outputs_safe(void) { event('S'); }
void rtl8735b_app_before_reset(void) { event('B'); }

// ---- 假 NOR Flash（ota_flash.c 的替身） ----
static uint8_t *flash;
static int lock_calls[8];
static int n_lock_calls;

static int fk_read(void *ctx, uint32_t a, uint8_t *b, uint32_t n)
{
    (void)ctx;
    if ((uint64_t)a + n > FLASH_SIZE) { return -1; }
    memcpy(b, flash + a, n);
    return 0;
}

static int fk_erase(void *ctx, uint32_t a)
{
    (void)ctx;
    if (a % OTA_SECTOR_SIZE != 0 || (uint64_t)a + OTA_SECTOR_SIZE > FLASH_SIZE) { return -1; }
    memset(flash + a, 0xFF, OTA_SECTOR_SIZE);
    return 0;
}

static int program_fails;   // 非 0 時寫入一律失敗（模擬 Flash 寫入錯誤）

static int fk_program(void *ctx, uint32_t a, const uint8_t *b, uint32_t n)
{
    (void)ctx;
    if (program_fails || (uint64_t)a + n > FLASH_SIZE) { return -1; }
    for (uint32_t i = 0; i < n; i++) { flash[a + i] &= b[i]; }
    return 0;
}

static const ota_flash_t F = {fk_read, fk_erase, fk_program, NULL};

const ota_flash_t *rtl8735b_ota_flash(void)
{
    return &F;
}

void rtl8735b_ota_flash_use_lock(int use_lock)
{
    if (n_lock_calls < 8) { lock_calls[n_lock_calls++] = use_lock; }
}

uint32_t rtl8735b_ota_flash_size(void)
{
    return FLASH_SIZE;
}

// ---- TCP 與連線（ota_net.c、ota_session.c 的替身） ----
static int accept_pending;          // 下一次 accept 交出一條連線
static int serve_result = OTA_OK;
static int serve_reboot;
static int serve_probe;
static int serve_calls;
static int blocked_during_serve;

int ota_tcp_listen(uint16_t port) { return port == OTA_DEFAULT_PORT ? 3 : -1; }

int ota_tcp_accept(int listen_fd, uint32_t *address, uint16_t *port)
{
    (void)listen_fd;
    if (!accept_pending) { return OTA_NET_NONE; }
    accept_pending = 0;
    *address = 0xC0A889F4u;
    *port = 50000;
    return 4;
}

// 電腦端在第 peer_close_after 次 recv 時關閉連線（0：一直不關）；之前的 recv 都沒有資料
static int peer_close_after;
static int recv_calls;
static int closed_after_peer;       // 連線 fd 關閉時，對方是否已經關閉
static TickType_t closed_at_ms;

int ota_tcp_recv(int fd, uint8_t *buffer, uint16_t length)
{
    (void)fd; (void)buffer; (void)length;
    recv_calls++;
    return peer_close_after && recv_calls >= peer_close_after ? OTA_NET_ERR_CLOSED : 0;
}

int ota_tcp_send(int fd, const uint8_t *buffer, uint16_t length) { (void)fd; (void)buffer; return length; }

void ota_tcp_close(int fd)
{
    if (fd == 4) {
        closed_after_peer = peer_close_after && recv_calls >= peer_close_after;
        closed_at_ms = now_ms;
    }
}

// 真的 ota_session_serve() 在簽章驗過後設定 in_progress，要重開時保留，其他情況回傳前清除；試開探測不設
int ota_session_serve(ota_session_ctx_t *c)
{
    serve_calls++;
    CHECK(c->public_key == rtl8735b_ota_public_key && c->public_key_len == 32);
    c->probe = (uint8_t)serve_probe;
    c->in_progress = !serve_probe;
    blocked_during_serve = rtl8735b_app_arming_blocked();
    c->reboot = (uint8_t)serve_reboot;
    if (!serve_reboot) {
        c->in_progress = 0;
    }
    return serve_result;
}

// ---- 準備 Flash ----
static void put_le32(uint32_t a, uint32_t v)
{
    flash[a] = (uint8_t)v; flash[a + 1] = (uint8_t)(v >> 8); flash[a + 2] = (uint8_t)(v >> 16); flash[a + 3] = (uint8_t)(v >> 24);
}

// 分割表（fw1_addr、fw2_addr，長度 len）加上兩個有效的舊映像
static void setup_table(uint32_t fw1_addr, uint32_t fw2_addr, uint32_t len)
{
    flash = malloc(FLASH_SIZE);
    memset(flash, 0xFF, FLASH_SIZE);
    put_le32(OTA_PART_FW1_RECORD, fw1_addr);
    put_le32(OTA_PART_FW1_RECORD + 4, len);
    put_le32(OTA_PART_FW2_RECORD, fw2_addr);
    put_le32(OTA_PART_FW2_RECORD + 4, len);
    flash[OTA_PART_FW1_RECORD + OTA_PART_VALID_OFFSET] = OTA_PART_VALID;
    flash[OTA_PART_FW2_RECORD + OTA_PART_VALID_OFFSET] = OTA_PART_VALID;
    memcpy(flash + fw1_addr, ota_label, OTA_LABEL_LEN);
    memcpy(flash + fw2_addr, ota_label, OTA_LABEL_LEN);
}

static void setup_flash(void)
{
    setup_table(FW1_ADDR, FW2_ADDR, SLOT_LEN);
}

static const ota_layout_t LAYOUT = {{{FW1_ADDR, SLOT_LEN}, {FW2_ADDR, SLOT_LEN}}, FLASH_SIZE};

// 從 fw1 經 OTA 寫進 fw2（以真的 ota_core.c），之後從 fw2 開機就是試開中
static void install_into_fw2(void)
{
    enum { LEN = 20000 };
    static uint8_t img[LEN];
    ota_signed_t sm;
    uint32_t sum = 0;
    ota_slot_t slots[2];
    ota_session_t s;
    memcpy(img, ota_label, OTA_LABEL_LEN);
    for (uint32_t i = OTA_LABEL_LEN; i < LEN; i++) { img[i] = (uint8_t)(i * 7u); }
    for (uint32_t i = 0; i < LEN; i++) { sum += img[i]; }
    const uint8_t h[OTA_HEADER_LEN] = {'R', 'O', 'T', 'A', OTA_VERSION, 0, 0, 0,
                                       (uint8_t)LEN, (uint8_t)(LEN >> 8), 0, 0,
                                       (uint8_t)sum, (uint8_t)(sum >> 8), (uint8_t)(sum >> 16), (uint8_t)(sum >> 24)};
    memcpy(sm.header, h, sizeof(h));
    crypto_blake2b(sm.hash, img, LEN);
    CHECK(ota_read_slots(&F, &LAYOUT, slots) == OTA_OK);
    CHECK(ota_begin(&s, slots, 1, LEN, sum) == OTA_OK);
    CHECK(ota_auth_start(&s, &sm) == OTA_OK);
    CHECK(ota_write(&s, &F, img, LEN) == OTA_OK);
    CHECK(ota_finish(&s, &F) == OTA_OK);
    running_idx = 2;
}

static int has(const char *text)
{
    return strstr(out, text) != NULL;
}

// ---- 案例 ----

static void normal_boot(void)
{
    setup_flash();
    rtl8735b_app_init();
    // 沒有任何 task（不帶開機時的備援接收端）、沒有試開計時器
    CHECK(n_created == 0 && timer_cb == NULL);
    CHECK(!rtl8735b_app_arming_blocked());
    CHECK(!has("OTA: fw"));
    // 開機檢查在排程器啟動前：不上鎖，做完恢復上鎖
    CHECK(n_lock_calls == 2 && lock_calls[0] == 0 && lock_calls[1] == 1);
}

static void trial_boot_blocks_arming_and_starts_checks(void)
{
    setup_flash();
    install_into_fw2();
    rtl8735b_app_init();
    CHECK(has("OTA: fw2 trial boot 1 of 3\r\n"));
    CHECK(timer_cb != NULL);
    CHECK(count_named("ota_confirm") == 1);
    CHECK(created[0].priority == tskIDLE_PRIORITY + 1);
    CHECK(rtl8735b_app_arming_blocked());
}

// 試開中的映像，開機時記試開次數的寫入失敗（OTA_BOOT_ERROR）：仍在試開、沒確認，不能解鎖
static void trial_check_failure_blocks_arming(void)
{
    setup_flash();
    install_into_fw2();
    program_fails = 1;
    rtl8735b_app_init();
    CHECK(has("OTA: trial check failed"));
    CHECK(rtl8735b_app_arming_blocked());
}

static void rollback_resets_before_anything_else(void)
{
    uint32_t attempts = 0;
    ota_slot_t slots[2];
    setup_flash();
    install_into_fw2();
    CHECK(ota_read_slots(&F, &LAYOUT, slots) == OTA_OK);
    for (uint32_t k = 0; k < OTA_TRIAL_MAX_ATTEMPTS; k++) {
        CHECK(ota_boot_check(&F, slots, 2, &attempts) == OTA_BOOT_TRIAL);
    }
    const int r = setjmp(escape);
    if (r == ESC_NONE) {
        rtl8735b_app_init();
        CHECK(0);   // 不應回到這裡
    }
    CHECK(r == ESC_RESET);
    CHECK(has("OTA: fw2 not confirmed after 3 trial boots, falling back\r\n"));
    // 排程器還沒啟動：不通知其他模組、不經輸出的鎖
    CHECK(strcmp(events, "R") == 0);
}

static void layout_mismatch_is_reported(void)
{
    // 分割表是 SDK 舊版面，韌體是 realtek-ota
    setup_table(0x80000u, 0x4C0000u, 0x380000u);
    rtl8735b_app_init();
    CHECK(has("OTA: partition table does not match flash layout realtek-ota (flash 16777216 bytes), updates disabled\r\n"));
    CHECK(!rtl8735b_app_arming_blocked());
    CHECK(n_created == 0);
}

// 接收端 listen 一次（之後 accept 沒有連線，用完延遲次數就跳出）
static void listen_once(void)
{
    rtl8735b_app_after_setup();
    CHECK(run_task(task_named("ota"), 1) == ESC_BUDGET);
    CHECK(has("OTA: listening on TCP 8266\r\n"));
}

static void confirm_after_60_s_when_ready(void)
{
    setup_flash();
    install_into_fw2();
    rtl8735b_app_init();
    listen_once();
    const TickType_t start = now_ms;
    CHECK(run_task(task_named("ota_confirm"), 1000) == ESC_DELETE);
    CHECK(now_ms - start >= OTA_CONFIRM_AFTER_MS);
    CHECK(has("OTA: fw2 confirmed\r\n"));
    CHECK(timer_stopped);
    CHECK(!rtl8735b_app_arming_blocked());
}

static void no_confirm_without_listening(void)
{
    setup_flash();
    install_into_fw2();
    rtl8735b_app_init();
    CHECK(run_task(task_named("ota_confirm"), 200) == ESC_BUDGET);
    CHECK(!has("confirmed"));
    CHECK(rtl8735b_app_arming_blocked());
}

static void no_confirm_when_main_loop_stalls(void)
{
    // 500 ms 是 ota_app.c 的門檻（要小於它才算健康）；UINT32_MAX 是主迴圈還沒跑完過一圈
    const uint32_t stalls[] = {500u, UINT32_MAX};
    setup_flash();
    install_into_fw2();
    rtl8735b_app_init();
    listen_once();
    for (size_t i = 0; i < sizeof(stalls) / sizeof(stalls[0]); i++) {
        ms_since_main_loop = stalls[i];
        CHECK(run_task(task_named("ota_confirm"), 200) == ESC_BUDGET);
        CHECK(!has("confirmed"));
    }
    ms_since_main_loop = 10;
    system_initialized = 0;
    CHECK(run_task(task_named("ota_confirm"), 200) == ESC_BUDGET);
    CHECK(!has("confirmed"));
}

static void no_confirm_without_wifi(void)
{
    setup_flash();
    install_into_fw2();
    rtl8735b_app_init();
    listen_once();
    wifi_associated = 0;
    CHECK(run_task(task_named("ota_confirm"), 200) == ESC_BUDGET);
    CHECK(!has("confirmed"));
}

static void trial_timeout_resets_only_when_disarmed(void)
{
    setup_flash();
    install_into_fw2();
    rtl8735b_app_init();
    CHECK(timer_cb != NULL);
    now_ms = OTA_TRIAL_TIMEOUT_MS - 1000u;
    timer_cb(NULL);                     // 還沒到
    CHECK(n_events == 0);
    now_ms = OTA_TRIAL_TIMEOUT_MS + 1000u;
    armed = 1;
    timer_cb(NULL);                     // 解鎖中不重開
    CHECK(n_events == 0);
    armed = 0;
    timer_cb(NULL);                     // 上鎖後還有寬限
    CHECK(n_events == 0);
    now_ms += OTA_TRIAL_DISARM_GRACE_MS;
    const int r = setjmp(escape);
    if (r == ESC_NONE) {
        timer_cb(NULL);
        CHECK(0);
    }
    // 計時器回呼不能阻塞：先通知其他模組（它們的 before_reset 依約定不阻塞），不經輸出的鎖，再重置
    CHECK(r == ESC_RESET && strcmp(events, "BR") == 0);
}

static void reboot_order_after_install(void)
{
    setup_flash();
    rtl8735b_app_init();
    rtl8735b_app_after_setup();
    accept_pending = 1;
    serve_reboot = 1;
    peer_close_after = 3;
    CHECK(run_task(task_named("ota"), 10) == ESC_RESET);
    CHECK(serve_calls == 1 && blocked_during_serve);
    // 等電腦讀到最後的狀態、關閉連線之後才關閉並重開
    CHECK(recv_calls == 3 && closed_after_peer);
    CHECK(has("OTA: 192.168.137.244: image installed (0)\r\n"));
    CHECK(has("OTA: rebooting\r\n"));
    // 先讓其他模組進入可以重置的狀態，再讓輸出安全，最後重置
    CHECK(strcmp(events, "BSR") == 0);
}

// 電腦一直不關閉連線（例如它已經離開）：最多等 OTA_PEER_CLOSE_WAIT_MS 就照常重開
static void reboot_after_peer_close_timeout(void)
{
    setup_flash();
    rtl8735b_app_init();
    rtl8735b_app_after_setup();
    accept_pending = 1;
    serve_reboot = 1;
    const TickType_t t0 = now_ms;
    CHECK(run_task(task_named("ota"), 5000) == ESC_RESET);
    CHECK(!closed_after_peer && recv_calls > 1);
    CHECK(closed_at_ms - t0 >= 10000 && closed_at_ms - t0 < 10000 + 50);
    CHECK(strcmp(events, "BSR") == 0);
}

static void reboot_skipped_when_armed(void)
{
    setup_flash();
    rtl8735b_app_init();
    rtl8735b_app_after_setup();
    accept_pending = 1;
    serve_reboot = 1;
    peer_close_after = 1;
    armed = 1;
    CHECK(run_task(task_named("ota"), 3) == ESC_BUDGET);
    CHECK(has("OTA: armed, reboot skipped\r\n"));
    CHECK(n_events == 0);
    CHECK(!rtl8735b_app_arming_blocked());
}

static void refused_update_clears_progress(void)
{
    setup_flash();
    rtl8735b_app_init();
    rtl8735b_app_after_setup();
    accept_pending = 1;
    serve_result = OTA_ERR_AUTH;
    CHECK(run_task(task_named("ota"), 3) == ESC_BUDGET);
    CHECK(blocked_during_serve);
    CHECK(has("OTA: 192.168.137.244: refused or failed (12)\r\n"));
    CHECK(!rtl8735b_app_arming_blocked() && n_events == 0);
    // 不重開時不等電腦關閉，直接關閉連線
    CHECK(recv_calls == 0);
}

static void receiver_starts_once(void)
{
    setup_flash();
    rtl8735b_app_init();
    rtl8735b_app_after_setup();
    rtl8735b_app_after_setup();
    CHECK(count_named("ota") == 1);
    CHECK(!has("no public key"));
}

// 試開探測：連線紀錄寫「trial probe answered」，不重開、不擋解鎖
static void probe_is_logged(void)
{
    setup_flash();
    rtl8735b_app_init();
    rtl8735b_app_after_setup();
    accept_pending = 1;
    serve_probe = 1;
    CHECK(run_task(task_named("ota"), 3) == ESC_BUDGET);
    CHECK(serve_calls == 1 && !blocked_during_serve);
    CHECK(has("OTA: 192.168.137.244: trial probe answered (0)\r\n"));
    CHECK(n_events == 0 && !rtl8735b_app_arming_blocked());
}

static void no_listen_without_wifi(void)
{
    setup_flash();
    rtl8735b_app_init();
    wifi_associated = 0;
    rtl8735b_app_after_setup();
    CHECK(run_task(task_named("ota"), 5) == ESC_BUDGET);
    CHECK(!has("listening"));
}

typedef struct { const char *name; void (*fn)(void); } test_case_t;

static const test_case_t CASES[] = {
    {"normal_boot", normal_boot},
    {"trial_boot_blocks_arming_and_starts_checks", trial_boot_blocks_arming_and_starts_checks},
    {"trial_check_failure_blocks_arming", trial_check_failure_blocks_arming},
    {"rollback_resets_before_anything_else", rollback_resets_before_anything_else},
    {"layout_mismatch_is_reported", layout_mismatch_is_reported},
    {"confirm_after_60_s_when_ready", confirm_after_60_s_when_ready},
    {"no_confirm_without_listening", no_confirm_without_listening},
    {"no_confirm_when_main_loop_stalls", no_confirm_when_main_loop_stalls},
    {"no_confirm_without_wifi", no_confirm_without_wifi},
    {"trial_timeout_resets_only_when_disarmed", trial_timeout_resets_only_when_disarmed},
    {"reboot_order_after_install", reboot_order_after_install},
    {"reboot_after_peer_close_timeout", reboot_after_peer_close_timeout},
    {"reboot_skipped_when_armed", reboot_skipped_when_armed},
    {"refused_update_clears_progress", refused_update_clears_progress},
    {"receiver_starts_once", receiver_starts_once},
    {"no_listen_without_wifi", no_listen_without_wifi},
    {"probe_is_logged", probe_is_logged},
};

// ---- 跨開機的序列：每次開機是一個行程，假 Flash 存成檔案留到下一次 ----
// 用來驗證新映像的接收端沒起來時，只靠試開逾時與退回就回到舊槽（維護者不在現場時，第一次上板唯一的退路），
// 以及健康的新映像確認之後，下一次開機不再試開。由 test_ota_app.py 依序執行：
//   seq_install <檔>            舊接收端（fw1 上）把新映像裝進 fw2
//   seq_labels <檔>             印出兩槽的標籤是否有效（bootloader 依此選槽）
//   seq_boot <檔> <1|2> dead    開機；接收端一直沒起來，時間走到試開逾時，計時器回呼重置
//   seq_boot <檔> <1|2> healthy 開機；接收端 listen、主迴圈健康，約 60 秒後確認

static void load_flash(const char *path)
{
    FILE *f = fopen(path, "rb");
    flash = malloc(FLASH_SIZE);
    CHECK(f != NULL && flash != NULL);
    CHECK(fread(flash, 1, FLASH_SIZE, f) == FLASH_SIZE);
    fclose(f);
}

static void save_flash(const char *path)
{
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    CHECK(fwrite(flash, 1, FLASH_SIZE, f) == FLASH_SIZE);
    fclose(f);
}

static int seq_install(const char *path)
{
    setup_flash();
    install_into_fw2();
    save_flash(path);
    fputs("installed\n", stdout);
    return 0;
}

static int seq_labels(const char *path)
{
    ota_slot_t slots[2];
    load_flash(path);
    CHECK(ota_read_slots(&F, &LAYOUT, slots) == OTA_OK);
    fprintf(stdout, "fw1 %d fw2 %d\n", ota_label_valid(&F, &slots[0]), ota_label_valid(&F, &slots[1]));
    return 0;
}

static int seq_boot(const char *path, int running, int healthy)
{
    load_flash(path);
    running_idx = (uint32_t)running;
    int r = setjmp(escape);
    if (r == ESC_NONE) {
        rtl8735b_app_init();
        if (healthy && task_named("ota_confirm") != NULL) {
            rtl8735b_app_after_setup();
            CHECK(run_task(task_named("ota"), 1) == ESC_BUDGET);
            r = run_task(task_named("ota_confirm"), 1000);
        } else if (timer_cb != NULL) {
            // 接收端與確認都沒有發生：只剩計時器回呼
            now_ms = OTA_TRIAL_TIMEOUT_MS + 1000u;
            timer_cb(NULL);
        }
    }
    save_flash(path);
    fputs(out, stdout);
    fputs(r == ESC_RESET ? "result reset\n" : "result running\n", stdout);
    fputs(rtl8735b_app_arming_blocked() ? "blocked 1\n" : "blocked 0\n", stdout);
    fprintf(stdout, "events %s\n", n_events ? events : "-");
    return 0;
}

int main(int argc, char **argv)
{
    const size_t n = sizeof(CASES) / sizeof(CASES[0]);
    if (argc == 3 && strcmp(argv[1], "seq_install") == 0) {
        return seq_install(argv[2]);
    }
    if (argc == 3 && strcmp(argv[1], "seq_labels") == 0) {
        return seq_labels(argv[2]);
    }
    if (argc == 5 && strcmp(argv[1], "seq_boot") == 0) {
        return seq_boot(argv[2], atoi(argv[3]), strcmp(argv[4], "healthy") == 0);
    }
    if (argc < 2) {
        for (size_t i = 0; i < n; i++) {
            fputs(CASES[i].name, stdout);
            fputs("\n", stdout);
        }
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        if (strcmp(argv[1], CASES[i].name) == 0) {
            CASES[i].fn();
            fputs("ok\n", stdout);
            return 0;
        }
    }
    fputs("unknown case\n", stdout);
    return 2;
}
