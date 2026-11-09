/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

/*
 * 一條 OTA 連線的協定（標頭版本 2，公鑰簽章）。網路、時鐘與解鎖狀態由呼叫端以函式指標提供，
 * 可在主機上測試。狀態碼都是 4 bytes little-endian（ota_core.h 的 enum ota_status）。
 *
 *   1. 板子：狀態 OK。沒有公鑰時送 NO_KEY 後關閉；已解鎖時什麼都不送，直接關閉。
 *   2. 電腦：16 bytes 標頭 ‖ 64 bytes 映像的 BLAKE2b-512 雜湊 ‖ 64 bytes 簽章（簽「標頭 ‖ 雜湊」）。
 *      電腦端只送簽好的檔，不需要任何金鑰。
 *   3. 板子：先驗簽章（驗不過回 AUTH），再解析標頭；未解鎖、目前的映像不在試開中（否則 BUSY）、
 *      版面相同、映像放得下才回 OK；否則回錯誤並關閉。簽章驗過之前不碰 Flash。
 *   4. 電腦：映像（標頭寫的長度）。
 *   5. 板子：最後的狀態。OK 表示映像的雜湊和簽過的相同、讀回核對過，已寫進另一槽並啟用；
 *      雜湊不同回 BAD_IMAGE，不啟用。
 *
 * 試開探測（唯讀，電腦端不需金鑰）：第 2 步改送 16 bytes，開頭是 OTA_PROBE_MAGIC（"RPRB"），其餘不看。板子讀分割表與
 * 目前槽的試開紀錄，回 4 bytes 狀態 ‖ 4 bytes 目前開機的槽（1 或 2）後關閉：OK 表示目前的槽不在試開中（已確認，
 * 或本來就不是 OTA 裝的），BUSY 表示還在試開、等確認；版面不符等錯誤照常回報。不寫 Flash、不設「OTA 進行中」旗標。
 * 換好映像後用它確認板子從哪一槽開機、新映像確認了沒有。已解鎖時一樣什麼都不送；韌體沒有公鑰時第 1 步就回 NO_KEY。
 *
 * 協定沒有 nonce：簽章是對映像，不是對連線，錄下的更新可以原樣重送（見 ota_core.h）。
 *
 * 逾時：簽章驗過之前的期限從連線開始算，是絕對的（未驗證的連線最多佔 OTA_T_AUTH_MS）；
 * 之後是閒置逾時，另有從連線開始算的總上限。
 *
 * 「OTA 進行中」旗標 in_progress 在簽章驗過、標頭解析通過後設定。ota_session_serve 回傳時，只有要重開
 * （c->reboot）時旗標保持設定，讓解鎖一直被擋到重置；呼叫端不重置時自行清除。其他情況回傳前一定清除。
 * 解鎖狀態在連線開始、設定旗標之後、每寫一塊之前、收齊映像後、讀回核對之後（寫試開紀錄與標籤之前）、
 * 決定重開之前各檢查一次；讀到已解鎖就中止（映像已啟用時只是不重開）。
 * 映像已啟用但最後的狀態送不出去時照常決定是否重開；電腦端會回報安裝狀態不明。
 */

#include <stdint.h>

#include "ota_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_STATUS_LEN      4u
#define OTA_REQUEST_LEN     (OTA_AUTH_MSG_LEN + OTA_SIG_LEN)
#define OTA_PROBE_MAGIC     0x42525052u     // "RPRB"
#define OTA_PROBE_REPLY_LEN 8u
#define OTA_T_AUTH_MS       5000u
#define OTA_T_IDLE_MS       10000u
#define OTA_T_TOTAL_MS      300000u
#define OTA_RECV_CHUNK      1024u
#define OTA_PROGRESS_STEPS  20u     // ota_io_t.progress 每 1/20（5%）叫一次

typedef struct {
    // >0：收到的位元組數（不超過 len）；0：目前沒有資料；<0：連線關閉或錯誤
    int (*recv)(void *ctx, uint8_t *buf, uint32_t len);
    // 全部送出回傳 0
    int (*send)(void *ctx, const uint8_t *buf, uint32_t len);
    uint64_t (*now_us)(void *ctx);
    // 沒有資料時讓出 CPU 一小段時間
    void (*wait)(void *ctx);
    int (*armed)(void *ctx);
    void *ctx;
    // 選填，NULL 就不叫：收到的量每跨過一個 5%（OTA_PROGRESS_STEPS）叫一次，給呼叫端印診斷。
    // 在接收迴圈裡叫，不可阻塞太久
    void (*progress)(void *ctx, uint32_t received, uint32_t total);
} ota_io_t;

typedef struct {
    // 呼叫端設定
    const ota_io_t *io;
    const ota_flash_t *flash;
    // 編進韌體的版面與 Flash 容量；分割表要和它完全相同才接受更新
    const ota_layout_t *layout;
    // 編進韌體的公鑰；長度不是 OTA_PUBLIC_KEY_LEN 時一律回 NO_KEY
    const uint8_t *public_key;
    uint32_t public_key_len;
    int running_idx;
    // ota_session_serve 維護；in_progress 可由其他 task 讀
    volatile uint8_t in_progress;
    uint8_t reboot;
    // ota_session_serve 設定：這條連線是試開探測
    uint8_t probe;
    ota_session_t s;
    uint8_t buf[OTA_RECV_CHUNK];
} ota_session_ctx_t;

// 標頭開頭是試開探測的 magic
int ota_is_probe(const uint8_t buf[OTA_HEADER_LEN]);

// 服務一條已建立的連線，回傳結果（enum ota_status）。呼叫端之後關閉連線；c->reboot 時重開，
// 不重開就清除 c->in_progress
int ota_session_serve(ota_session_ctx_t *c);

#ifdef __cplusplus
}
#endif
