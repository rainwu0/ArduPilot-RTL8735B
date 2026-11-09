/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

/*
 * Wi-Fi 韌體更新（OTA）的核心（公鑰簽章）：Flash 由呼叫端以函式指標提供，可在主機上測試。
 *
 * 依官方公開文件（AmebaPro2 SDK 文件〈09 OTA〉，
 * https://ameba-doc-rtos-pro2-sdk.readthedocs-hosted.com/en/latest/application_note/09_OTA.html）：
 *   - Flash 有兩個韌體槽；分割表在 0x2060、0x2080 各記錄一個槽的位址與長度（little-endian u32）。
 *     每筆記錄 16 bytes，最後一個 byte 是 valid（建出的 flash_ntz.bin 中有效的記錄為 0x01）。
 *   - 每個槽開頭 8 bytes 是標籤 "RTL8735B"，bootloader 只從標籤完整的槽中，依版本與時間戳選最新的。
 *   - 寫入時標籤先留成 0xFF，整個映像寫完、核對過才寫標籤，中斷的寫入不會被選中。
 *   - 映像的核對值是全部位元組的總和（mod 2^32），附在 ota.bin 尾端。
 * 本實作另外的規則：
 *   - 驗證用公鑰：韌體只帶公鑰（32 bytes），私鑰只在電腦端簽章時用。簽章是 Monocypher 的 EdDSA
 *     （curve25519 + BLAKE2b，和 ArduPilot 的 AP_CheckFirmware、Tools/scripts/signing 同一套）。
 *   - 簽的訊息是「標頭 16 bytes ‖ 映像的 BLAKE2b-512 雜湊 64 bytes」（OTA_AUTH_MSG_LEN）。
 *     簽章通過才解析標頭、開始抹寫（ota_auth_header()）；收映像時邊收邊算雜湊，收完和簽過的雜湊相同，
 *     標籤正確、讀回 Flash 的位元組總和和標頭相同，才寫試開紀錄與標籤（ota_verify()、ota_activate()）。
 *     雜湊不同回 OTA_ERR_BAD_IMAGE：另一槽寫過但沒有標籤，不會被選中；目前的槽一個位元都沒動。
 *   - 標頭開頭的 magic "ROTA" 也在簽章範圍內，同一把金鑰簽的 ArduPilot 韌體簽章不能拿來當 OTA 的簽章。
 *   - 沒有 nonce：錄下的合法更新可以原樣重送，也可以送舊的、簽過的映像（降版）。和 AP_CheckFirmware 相同。
 *   - 韌體沒有公鑰（長度不是 32）時一律拒絕，回 OTA_ERR_NO_KEY；不靠全 0 的公鑰驗不過。
 *   - OTA 只寫另一個槽：資料、槽尾的試開紀錄、最後的標籤；每次抹除與寫入都檢查位址。
 *   - 槽尾 4 KiB 保留給試開紀錄，映像上限是兩槽中較短者的長度減 4 KiB。
 *   - 新映像第一次開機起進入試開；每次開機記一次，健康運作後由韌體確認（ota_confirm_ready()：
 *     開機滿 OTA_CONFIRM_AFTER_MS、接收端可用、主迴圈健康、Wi-Fi 連上）。試開次數用完仍未確認，
 *     且另一槽有效時，把自己的標籤清成 0，重置後 bootloader 只會選另一槽。
 *   - 目前的槽還在試開時不接受下一次 OTA（ota_trial_pending()）：另一槽是唯一確認過的映像，不能蓋掉。
 *   - 試開中的映像超過 OTA_TRIAL_TIMEOUT_MS 仍未確認就重開（記下一次試開）；解鎖中絕不重開，
 *     上鎖後再等 OTA_TRIAL_DISARM_GRACE_MS（ota_trial_timeout_due()）。
 *   - 分割表要和編進韌體的版面（hwdef 的 FLASH_LAYOUT）完全相同，槽也要放得進 Flash 的實際容量，
 *     否則所有入口都拒絕：OTA 不能改分割表，版面不同的韌體不能用 OTA 更新。開機時怎麼處理試開
 *     由 ota_boot_plan() 決定：版面不同、但分割表的槽放得進 Flash 時，照分割表記錄試開但不確認，
 *     試開次數用完後退回；放不進時不寫 Flash，目前的槽在試開中就只擋解鎖。
 *   - 只在試開期間（OTA 寫完到確認成功之間）開機時寫 Flash；確認之後與平常的開機都不寫。
 *   - 試開紀錄與標籤的更新都只把位元由 1 清成 0，不抹除；斷電時不會留下半寫的狀態。
 */

#include <stdint.h>

#include "ota_monocypher.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_SECTOR_SIZE         4096u
#define OTA_LABEL_LEN           8u
#define OTA_PART_FW1_RECORD     0x2060u
#define OTA_PART_FW2_RECORD     0x2080u
#define OTA_TRIAL_MAX_ATTEMPTS  3u
#define OTA_CONFIRM_AFTER_MS    60000u

// 分割表的一筆記錄：位址、長度（little-endian u32），最後一個 byte 是 valid
#define OTA_PART_RECORD_LEN     16u
#define OTA_PART_VALID_OFFSET   15u
#define OTA_PART_VALID          0x01u

// 與版面無關的保護範圍：韌體槽必須完全落在 [OTA_SLOT_FLOOR, OTA_SLOT_CEILING) 內，否則當成分割表錯誤，
// 不讀寫任何槽。下限保護開機區與分割表（各版面的 boot_s 都在 0x57000 結束）；上限保護 SDK 的使用者資料
// （0xF00000 起，SDK project/realtek_amebapro2_v0_example/inc/platform_opts.h）、HAL 的參數區與更高的區域。
// 分割表讀錯或程式錯誤都不能讓 OTA 抹寫到這些地方；版面另由 ota_read_slots() 逐項比對。
#define OTA_SLOT_FLOOR          0x58000u
#define OTA_SLOT_CEILING        0xF00000u

// JEDEC ID 讀不到容量時假設的值（ota_flash_capacity()）：所有版面在建置時都以 16 MiB 驗證
// （hwdef/scripts/rtl8735b_flash_layout.py 的 FLASH_SIZE），HAL 的參數區（0xF80000 起）也需要 16 MiB
#define OTA_FLASH_ASSUMED_SIZE  0x1000000u

// 試開逾時：開機後這麼久仍未確認就重開；之前解鎖過時，上鎖後還要再等寬限
#define OTA_TRIAL_TIMEOUT_MS        300000u
#define OTA_TRIAL_DISARM_GRACE_MS   60000u

// 電腦端送來的標頭（little-endian）：magic "ROTA"、版本、旗標、映像長度、位元組總和
#define OTA_HEADER_LEN          16u
#define OTA_MAGIC               0x41544F52u
#define OTA_VERSION             2u
#define OTA_FLAG_NO_REBOOT      0x0001u

#define OTA_PUBLIC_KEY_LEN      32u
#define OTA_HASH_LEN            64u     // BLAKE2b-512
#define OTA_SIG_LEN             64u
// 簽章涵蓋的訊息：標頭 ‖ 映像的雜湊
#define OTA_AUTH_MSG_LEN        (OTA_HEADER_LEN + OTA_HASH_LEN)

// 槽尾的試開紀錄：magic、狀態、試開次數的位元圖（每次開機清掉最低一個為 1 的位元）、保留
#define OTA_TRIAL_RECORD_LEN    16u
#define OTA_TRIAL_MAGIC         0x4C525452u
#define OTA_TRIAL_PENDING       0xFFFFFFFEu
#define OTA_TRIAL_CONFIRMED     0x00000000u

// 也是回給電腦端的狀態碼；編號固定，電腦端照它解讀
enum ota_status {
    OTA_OK = 0,
    OTA_ERR_BAD_HEADER = 1,
    OTA_ERR_ARMED = 2,
    OTA_ERR_TOO_LARGE = 3,
    OTA_ERR_BUSY = 4,
    OTA_ERR_PARTITION = 5,
    OTA_ERR_CHECKSUM = 6,
    OTA_ERR_FLASH = 7,
    OTA_ERR_TIMEOUT = 8,
    OTA_ERR_RANGE = 9,
    OTA_ERR_STATE = 10,
    OTA_ERR_BAD_IMAGE = 11,     // 標籤錯，或收到的映像和簽過的雜湊不同
    OTA_ERR_AUTH = 12,          // 簽章驗不過
    OTA_ERR_NO_KEY = 13,        // 韌體沒有公鑰
    OTA_ERR_RNG = 14,           // 保留不用
    OTA_ERR_IO = 15,            // 連線中斷或送不出去；只在板子端記錄
    OTA_ERR_LAYOUT = 16,        // 分割表與編進韌體的版面不同，或槽超出 Flash 的實際容量
};

enum ota_boot_action {
    OTA_BOOT_NORMAL = 0,        // 沒有試開紀錄，或已確認
    OTA_BOOT_TRIAL = 1,         // 試開中，已記一次
    OTA_BOOT_ROLLBACK = 2,      // 試開次數用完，已清除自己的標籤，呼叫端要重置
    OTA_BOOT_NO_FALLBACK = 3,   // 試開次數用完，但另一槽無效，只能繼續跑
    OTA_BOOT_ERROR = 4,         // 讀寫失敗，照常開機
};

// 開機時怎麼處理試開，由 ota_read_slots() 的結果決定（ota_boot_plan()）
enum ota_boot_plan {
    OTA_PLAN_SKIP = 0,          // 分割表讀不到或不合理：不讀試開紀錄、不寫 Flash
    OTA_PLAN_CHECK = 1,         // 版面相同：ota_boot_check()，試開中的映像健康運作後確認
    OTA_PLAN_BLOCK_ONLY = 2,    // 版面不同且槽放不進 Flash：不寫 Flash；目前的槽在試開中（ota_trial_pending()）時只擋解鎖
    OTA_PLAN_CHECK_NO_CONFIRM = 3,  // 版面不同但槽放得進 Flash：ota_boot_check() 照分割表記錄試開與退回，不確認
};

typedef struct {
    uint32_t addr;
    uint32_t len;
} ota_slot_t;

// 編進韌體的版面（fw1、fw2）與 Flash 的實際容量（bytes，0 表示讀不到）
typedef struct {
    ota_slot_t fw[2];
    uint32_t flash_size;
} ota_layout_t;

// ota_trial_timeout_due() 的狀態；開始前清成 0
typedef struct {
    uint32_t disarmed_at_ms;
    uint8_t was_armed;
    uint8_t disarmed_at_valid;
} ota_trial_timer_t;

// 成功回傳 0；erase_sector 抹除位址所在的 4 KiB；program 只把位元由 1 清成 0
typedef struct {
    int (*read)(void *ctx, uint32_t addr, uint8_t *buf, uint32_t len);
    int (*erase_sector)(void *ctx, uint32_t addr);
    int (*program)(void *ctx, uint32_t addr, const uint8_t *buf, uint32_t len);
    void *ctx;
} ota_flash_t;

typedef struct {
    uint16_t flags;
    uint32_t image_len;
    uint32_t checksum;
} ota_header_t;

// 簽章驗過的標頭與雜湊；只由 ota_auth_header() 填，ota_auth_start() 只收這個型別
typedef struct {
    uint8_t header[OTA_HEADER_LEN];
    uint8_t hash[OTA_HASH_LEN];
} ota_signed_t;

typedef struct {
    ota_slot_t target;
    ota_slot_t running;
    uint32_t image_len;
    uint32_t checksum;
    uint32_t received;
    crypto_blake2b_ctx hash;
    uint8_t expected[OTA_HASH_LEN];
    uint8_t label[OTA_LABEL_LEN];
    uint8_t active;
    uint8_t authed;
    uint8_t verified;
} ota_session_t;

extern const uint8_t ota_label[OTA_LABEL_LEN];

// 讀分割表的兩個槽。記錄無效或超出保護範圍回傳 OTA_ERR_PARTITION；
// 和 layout 不同、或超出 layout->flash_size 回傳 OTA_ERR_LAYOUT，這時 slots 仍是分割表的記錄（已通過保護範圍的檢查）
int ota_read_slots(const ota_flash_t *f, const ota_layout_t *layout, ota_slot_t slots[2]);
// 兩個槽都在 Flash 的實際容量內；flash_size 為 0 時一律不成立
int ota_slots_fit(const ota_slot_t slots[2], uint32_t flash_size);
// JEDEC ID 讀到的容量；讀不到（0）時回傳 OTA_FLASH_ASSUMED_SIZE
uint32_t ota_flash_capacity(uint32_t measured);
// read_result 是 ota_read_slots() 的回傳值，slots 與 flash_size 是它用的；回傳 enum ota_boot_plan
int ota_boot_plan(int read_result, const ota_slot_t slots[2], uint32_t flash_size);
uint32_t ota_image_limit(const ota_slot_t slots[2]);
int ota_label_valid(const ota_flash_t *f, const ota_slot_t *slot);
int ota_parse_header(const uint8_t buf[OTA_HEADER_LEN], ota_header_t *h);

// 驗 msg（標頭 ‖ 雜湊）的簽章。public_key_len 不是 OTA_PUBLIC_KEY_LEN 回傳 OTA_ERR_NO_KEY；
// 驗不過回傳 OTA_ERR_AUTH；通過時填 out。不解析標頭
int ota_auth_header(const uint8_t *public_key, uint32_t public_key_len, const uint8_t msg[OTA_AUTH_MSG_LEN],
                    const uint8_t sig[OTA_SIG_LEN], ota_signed_t *out);

// running_idx：目前開機的槽，1 或 2。ota_begin 之後要先 ota_auth_start 才能寫
int ota_begin(ota_session_t *s, const ota_slot_t slots[2], int running_idx, uint32_t image_len, uint32_t checksum);
// 記下簽過的雜湊，開始算收到的映像的雜湊
int ota_auth_start(ota_session_t *s, const ota_signed_t *signed_msg);
int ota_range_allowed(const ota_session_t *s, uint32_t addr, uint32_t len);
int ota_write(ota_session_t *s, const ota_flash_t *f, const uint8_t *data, uint32_t len);
// 依序核對收到的映像的雜湊（和簽過的相同）、標籤、位元組總和（讀回整個映像），不寫 Flash；通過後工作階段仍在進行
int ota_verify(ota_session_t *s, const ota_flash_t *f);
// ota_verify 通過後才可呼叫：寫試開紀錄，最後寫標籤，結束工作階段
int ota_activate(ota_session_t *s, const ota_flash_t *f);
// ota_verify 加 ota_activate
int ota_finish(ota_session_t *s, const ota_flash_t *f);
// 中途放棄：結束工作階段並清除雜湊狀態
void ota_abort(ota_session_t *s);

int ota_boot_check(const ota_flash_t *f, const ota_slot_t slots[2], int running_idx, uint32_t *attempts);
int ota_confirm(const ota_flash_t *f, const ota_slot_t slots[2], int running_idx);
// 目前的槽在試開期間（有紀錄且未確認）回傳 1，否則 0；讀取失敗回傳 -1。不寫 Flash
int ota_trial_pending(const ota_flash_t *f, const ota_slot_t slots[2], int running_idx);
// 試開中的映像可以確認時回傳 1。receiver_ready：這個映像能再接收下一次 OTA（接收端已開始、有公鑰）
int ota_confirm_ready(uint32_t since_boot_ms, int receiver_ready, int main_loop_healthy, int wifi_associated);
// 試開中的映像現在該重開時回傳 1。每次呼叫帶入開機後的時間、是否已確認、目前是否解鎖：
// 已確認不重開；解鎖中不重開；未到 OTA_TRIAL_TIMEOUT_MS 不重開；之前解鎖過時，上鎖後未滿寬限不重開
int ota_trial_timeout_due(ota_trial_timer_t *t, uint32_t since_boot_ms, int confirmed, int armed);

#ifdef __cplusplus
}
#endif
