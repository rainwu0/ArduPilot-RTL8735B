/*
 * ota_session.c（公鑰簽章）的主機測試：假連線（依假時鐘送出電腦端的資料流，可停住、關閉、
 * 慢慢送）、假時鐘（每次 wait 前進 10 ms）、假 NOR Flash 與可設定的解鎖狀態。電腦端的簽章用 Monocypher 現算。
 * 每個拒收的案例都核對「目前的槽與分割表一個位元組都沒變、模擬重開後開的還是原來的槽」（stays_on_running_slot）。
 * 用法：ota_session_test <案例>，通過時印 ok 並以 0 結束；不帶參數時列出案例。
 */
#include "ota_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLASH_SIZE 0x1000000u
#define FW1_ADDR 0x80000u
#define FW2_ADDR 0x4C0000u
#define SLOT_LEN 0x380000u
#define TAIL (FW2_ADDR + SLOT_LEN - OTA_SECTOR_SIZE)
#define IMG_LEN 20000u
#define WAIT_US 10000u
#define SEC 1000000ull
#define NEVER 0xFFFFFFFFu
#define REQ_LEN OTA_REQUEST_LEN
// 分割表所在的那一塊（0x2060、0x2080 的記錄都在裡面）
#define PART_ADDR 0x2000u
#define PART_LEN 0x100u

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __func__, __LINE__, #c); exit(1); } } while (0)

// 簽章用的私鑰（Monocypher 的 32 bytes 種子）；板子只有 SECRET 的公鑰。SECRET2 是另一把
static const uint8_t SECRET[32] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
};
static const uint8_t SECRET2[32] = {
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
};
static uint8_t public_key[OTA_PUBLIC_KEY_LEN];

// 假 NOR Flash；snapshot 是 serve() 之前的分割表與目前的槽（fw1，含槽尾的試開紀錄）
static uint8_t *flash;
static uint8_t *snap_part, *snap_running;
static int flash_ops;
static uint32_t fail_program_from;

// 假時鐘
static uint64_t now_us;

// 電腦端：標頭 ‖ 雜湊 ‖ 簽章 ‖ 映像
static uint8_t *image;
static uint32_t image_sum;
static uint8_t *client;
static uint32_t client_len;
static uint32_t delivered;
static int client_built;
static uint32_t stall_at;       // 送到這個位置就不再送
static int close_at_stall;      // 停住時關閉連線
static uint32_t chunk;          // 每次最多送出的位元組
static uint64_t gap_us;         // 兩次送出的間隔
static uint32_t gap_from;       // 送到這個位置之後才有間隔
static uint64_t next_at;
static uint16_t hdr_flags;
static uint32_t hdr_len;        // 標頭寫的映像長度
static uint32_t hashed_len;     // 雜湊涵蓋的長度（平常和 hdr_len 相同）
static const uint8_t *sign_with;
static int bad_magic;           // 簽章前改 magic（簽章對，標頭錯）
static int flip_header_bit;     // 簽章後改標頭
static int flip_hash_byte;      // 簽章後改雜湊
static int flip_sig_byte;       // 改簽章
static int flip_image_byte;     // 雜湊與簽章之後改映像
static int probe_mode;         // 只送 16 bytes 的試開探測
static const uint8_t *replay;   // 原樣送出錄下的資料流
static uint32_t replay_len;

// 板子送出的資料
static uint8_t sent[64];
static uint32_t n_sent;
static uint32_t send_fail_from;

// 解鎖與旗標的觀察
static int armed_at_accept;
static uint32_t armed_after_delivered;
static uint32_t armed_after_sent;
static int armed_on_readback;   // 收齊之後讀回映像時才解鎖
static int armed_now;
static int armed_calls, armed_calls_with_flag;
static int flag_seen;

static ota_session_ctx_t ctx;

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void sample_flag(void)
{
    if (ctx.in_progress) { flag_seen = 1; }
}

static int fk_read(void *c, uint32_t a, uint8_t *b, uint32_t n)
{
    (void)c;
    if ((uint64_t)a + n > FLASH_SIZE) { return -1; }
    if (armed_on_readback && client_built && delivered == client_len &&
        a >= FW2_ADDR + OTA_LABEL_LEN && a < FW2_ADDR + IMG_LEN) {
        armed_now = 1;
    }
    memcpy(b, flash + a, n);
    return 0;
}

static int fk_erase(void *c, uint32_t a)
{
    (void)c;
    if (a % OTA_SECTOR_SIZE != 0 || (uint64_t)a + OTA_SECTOR_SIZE > FLASH_SIZE) { return -1; }
    memset(flash + a, 0xFF, OTA_SECTOR_SIZE);
    flash_ops++;
    return 0;
}

static int fk_program(void *c, uint32_t a, const uint8_t *b, uint32_t n)
{
    (void)c;
    if ((uint64_t)a + n > FLASH_SIZE || a >= fail_program_from) { return -1; }
    for (uint32_t i = 0; i < n; i++) { flash[a + i] &= b[i]; }
    flash_ops++;
    return 0;
}

static const ota_flash_t F = {fk_read, fk_erase, fk_program, NULL};

static void make_header(uint8_t h[OTA_HEADER_LEN])
{
    put_le32(h, OTA_MAGIC);
    h[4] = OTA_VERSION; h[5] = 0;
    h[6] = (uint8_t)hdr_flags; h[7] = (uint8_t)(hdr_flags >> 8);
    put_le32(h + 8, hdr_len);
    put_le32(h + 12, image_sum);
    if (bad_magic) { h[0] = 'X'; }
}

// 電腦端：標頭 ‖ BLAKE2b(映像) ‖ 簽章(標頭 ‖ 雜湊) ‖ 映像。簽章工具與傳送工具分開時，傳送端送的就是這一串
static void build_client(void)
{
    client_built = 1;
    if (probe_mode) {
        client_len = OTA_HEADER_LEN;
        client = malloc(client_len);
        memset(client, 0xA5, client_len);
        put_le32(client, OTA_PROBE_MAGIC);
        return;
    }
    if (replay) {
        client = malloc(replay_len);
        memcpy(client, replay, replay_len);
        client_len = replay_len;
        return;
    }
    client_len = REQ_LEN + IMG_LEN;
    client = malloc(client_len);
    uint8_t *hdr = client, *hash = client + OTA_HEADER_LEN, *sig = client + OTA_AUTH_MSG_LEN, *img = client + REQ_LEN;
    make_header(hdr);
    crypto_blake2b(hash, image, hashed_len);
    crypto_sign(sig, sign_with, NULL, client, OTA_AUTH_MSG_LEN);
    memcpy(img, image, IMG_LEN);
    if (flip_header_bit) { hdr[12] ^= 0x01; }
    if (flip_hash_byte) { hash[OTA_HASH_LEN - 1] ^= 0x01; }
    if (flip_sig_byte) { sig[0] ^= 0x01; }
    if (flip_image_byte) { img[IMG_LEN / 2] ^= 0x01; }
}

static int fk_recv(void *c, uint8_t *buf, uint32_t len)
{
    (void)c;
    sample_flag();
    if (!client_built) {
        if (n_sent < OTA_STATUS_LEN || le32(sent) != OTA_OK) { return 0; }
        build_client();
    }
    if (delivered >= stall_at) { return close_at_stall ? -1 : 0; }
    if (delivered >= client_len || now_us < next_at) { return 0; }
    uint32_t n = len;
    if (n > chunk) { n = chunk; }
    if (n > client_len - delivered) { n = client_len - delivered; }
    if (n > stall_at - delivered) { n = stall_at - delivered; }
    memcpy(buf, client + delivered, n);
    delivered += n;
    next_at = delivered >= gap_from ? now_us + gap_us : now_us;
    return (int)n;
}

static int fk_send(void *c, const uint8_t *b, uint32_t len)
{
    (void)c;
    sample_flag();
    if (n_sent + len > send_fail_from) { return -1; }
    CHECK(n_sent + len <= sizeof(sent));
    memcpy(sent + n_sent, b, len);
    n_sent += len;
    return 0;
}

static uint64_t fk_now(void *c)
{
    (void)c;
    return now_us;
}

static void fk_wait(void *c)
{
    (void)c;
    sample_flag();
    now_us += WAIT_US;
}

static int fk_armed(void *c)
{
    (void)c;
    armed_calls++;
    if (ctx.in_progress) { armed_calls_with_flag++; }
    return armed_at_accept || armed_now || delivered >= armed_after_delivered || n_sent >= armed_after_sent;
}

static const ota_io_t IO = {fk_recv, fk_send, fk_now, fk_wait, fk_armed, NULL, NULL};

// 進度回呼：記下次數與每次的位元組數
static int progress_calls;
static uint32_t progress_last;
static uint32_t progress_total;

static void fk_progress(void *c, uint32_t received, uint32_t total)
{
    (void)c;
    CHECK(received > progress_last && received <= total);
    // 每次都跨過一個新的 5%
    CHECK((uint64_t)received * OTA_PROGRESS_STEPS / total > (uint64_t)progress_last * OTA_PROGRESS_STEPS / total);
    progress_calls++;
    progress_last = received;
    progress_total = total;
}

static const ota_io_t IO_PROGRESS = {fk_recv, fk_send, fk_now, fk_wait, fk_armed, NULL, fk_progress};

// 每條連線開始前：電腦端、板子送出的紀錄、時鐘歸零；Flash 保留
static void new_connection(void)
{
    free(client);
    client = NULL;
    client_len = 0;
    client_built = 0;
    delivered = 0;
    stall_at = NEVER;
    close_at_stall = 0;
    chunk = 4096;
    gap_us = 0;
    gap_from = 0;
    next_at = 0;
    replay = NULL;
    probe_mode = 0;
    n_sent = 0;
    send_fail_from = NEVER;
    armed_at_accept = 0;
    armed_after_delivered = NEVER;
    armed_after_sent = NEVER;
    armed_on_readback = 0;
    armed_now = 0;
    armed_calls = armed_calls_with_flag = 0;
    flag_seen = 0;
    flash_ops = 0;
    now_us = 0;
}

// 編進韌體的版面：和分割表相同（SDK 舊版面），16 MiB Flash
static ota_layout_t LAYOUT = {{{FW1_ADDR, SLOT_LEN}, {FW2_ADDR, SLOT_LEN}}, FLASH_SIZE};

// 分割表、fw1 有有效的映像（標籤之後是可辨認的內容）、fw2 空白；板子從 fw1 開機
static void setup(void)
{
    flash = malloc(FLASH_SIZE);
    memset(flash, 0xFF, FLASH_SIZE);
    put_le32(flash + OTA_PART_FW1_RECORD, FW1_ADDR);
    put_le32(flash + OTA_PART_FW1_RECORD + 4, SLOT_LEN);
    put_le32(flash + OTA_PART_FW2_RECORD, FW2_ADDR);
    put_le32(flash + OTA_PART_FW2_RECORD + 4, SLOT_LEN);
    flash[OTA_PART_FW1_RECORD + OTA_PART_VALID_OFFSET] = OTA_PART_VALID;
    flash[OTA_PART_FW2_RECORD + OTA_PART_VALID_OFFSET] = OTA_PART_VALID;
    memcpy(flash + FW1_ADDR, ota_label, OTA_LABEL_LEN);
    for (uint32_t i = OTA_LABEL_LEN; i < 65536; i++) { flash[FW1_ADDR + i] = (uint8_t)(i * 31); }
    fail_program_from = NEVER;
    image = malloc(IMG_LEN);
    memcpy(image, ota_label, OTA_LABEL_LEN);
    uint32_t x = 12345u;
    for (uint32_t i = OTA_LABEL_LEN; i < IMG_LEN; i++) { x = x * 1103515245u + 12345u; image[i] = (uint8_t)(x >> 16); }
    image_sum = 0;
    for (uint32_t i = 0; i < IMG_LEN; i++) { image_sum += image[i]; }
    hdr_flags = 0;
    hdr_len = IMG_LEN;
    hashed_len = IMG_LEN;
    sign_with = SECRET;
    bad_magic = flip_header_bit = flip_hash_byte = flip_sig_byte = flip_image_byte = 0;
    crypto_sign_public_key(public_key, SECRET);
    memset(&ctx, 0, sizeof(ctx));
    ctx.io = &IO;
    ctx.flash = &F;
    ctx.public_key = public_key;
    ctx.public_key_len = sizeof(public_key);
    ctx.running_idx = 1;
    ctx.layout = &LAYOUT;
    snap_part = malloc(PART_LEN);
    memcpy(snap_part, flash + PART_ADDR, PART_LEN);
    snap_running = malloc(SLOT_LEN);
    memcpy(snap_running, flash + FW1_ADDR, SLOT_LEN);
    new_connection();
}

// 回傳後旗標只在要重開時保持（呼叫端重置前一直擋住解鎖），其他情況一定已清除
static int serve(void)
{
    const int r = ota_session_serve(&ctx);
    CHECK(ctx.in_progress == ctx.reboot);
    return r;
}

// 拒收後的不變量：
//   - 分割表與目前的槽（含槽尾的試開紀錄、開頭的標籤）一個位元組都沒變；
//   - 另一槽沒有標籤、沒有試開紀錄，bootloader 不會選它；
//   - 模擬重開：開機檢查照常（不在試開中），只有 fw1 是可開的槽；
//   - 不重開。
static void stays_on_running_slot(void)
{
    ota_slot_t slots[2];
    uint32_t attempts = 99;
    CHECK(memcmp(flash + PART_ADDR, snap_part, PART_LEN) == 0);
    CHECK(memcmp(flash + FW1_ADDR, snap_running, SLOT_LEN) == 0);
    CHECK(ota_read_slots(&F, &LAYOUT, slots) == OTA_OK);
    CHECK(ota_label_valid(&F, &slots[0]));
    CHECK(!ota_label_valid(&F, &slots[1]));
    for (uint32_t i = 0; i < OTA_LABEL_LEN; i++) { CHECK(flash[FW2_ADDR + i] == 0xFF); }
    CHECK(le32(flash + TAIL) == 0xFFFFFFFFu);
    CHECK(ota_boot_check(&F, slots, 1, &attempts) == OTA_BOOT_NORMAL && attempts == 0);
    CHECK(ota_trial_pending(&F, slots, 1) == 0);
    CHECK(!ctx.reboot);
}

// 拒收而且 Flash 完全沒碰（簽章驗過之前、或驗過但標頭的檢查沒過）
static void untouched(void)
{
    CHECK(flash_ops == 0 && !flag_seen);
    stays_on_running_slot();
}

static void installed(void)
{
    CHECK(memcmp(flash + FW2_ADDR, image, IMG_LEN) == 0);
    CHECK(le32(flash + TAIL) == OTA_TRIAL_MAGIC);
}

static void success(void)
{
    CHECK(serve() == OTA_OK);
    CHECK(n_sent == 3 * OTA_STATUS_LEN);
    CHECK(le32(sent) == OTA_OK && le32(sent + 4) == OTA_OK && le32(sent + 8) == OTA_OK);
    installed();
    CHECK(ctx.reboot);
    // 目前的槽沒被動過；重開後 fw2 進入試開
    CHECK(memcmp(flash + FW1_ADDR, snap_running, SLOT_LEN) == 0);
    ota_slot_t slots[2];
    uint32_t attempts = 0;
    CHECK(ota_read_slots(&F, &LAYOUT, slots) == OTA_OK);
    CHECK(ota_label_valid(&F, &slots[1]));
    CHECK(ota_boot_check(&F, slots, 2, &attempts) == OTA_BOOT_TRIAL && attempts == 1);
}

// 診斷：每跨過 5% 叫一次進度回呼，最後一次是收完的時候；回呼不影響安裝
static void progress_every_5_percent(void)
{
    ctx.io = &IO_PROGRESS;
    CHECK(serve() == OTA_OK);
    installed();
    CHECK(progress_total == IMG_LEN && progress_last == IMG_LEN);
    CHECK(progress_calls >= (int)(IMG_LEN / OTA_RECV_CHUNK) && progress_calls <= (int)OTA_PROGRESS_STEPS);
}

static void success_no_reboot_flag(void)
{
    hdr_flags = OTA_FLAG_NO_REBOOT;
    CHECK(serve() == OTA_OK);
    installed();
    CHECK(!ctx.reboot);
}

// 寫完、送出最後的狀態之後才解鎖：映像已裝好，但不重開
static void armed_before_reboot(void)
{
    armed_after_sent = 3 * OTA_STATUS_LEN;
    CHECK(serve() == OTA_OK);
    installed();
    CHECK(!ctx.reboot);
}

// 簽章驗過後，每次檢查解鎖時旗標都已設定（只有 accept 時的檢查在旗標之前）
static void flag_set_during_data(void)
{
    chunk = 1000;
    CHECK(serve() == OTA_OK);
    CHECK(armed_calls >= (int)(IMG_LEN / 1000));
    CHECK(armed_calls_with_flag == armed_calls - 1);
}

static void armed_at_accept_closes_silently(void)
{
    armed_at_accept = 1;
    CHECK(serve() == OTA_ERR_ARMED);
    CHECK(n_sent == 0);
    untouched();
}

// 韌體沒有公鑰（建置時沒指定）：一律拒收
static void no_key(void)
{
    ctx.public_key_len = 0;
    CHECK(serve() == OTA_ERR_NO_KEY);
    CHECK(n_sent == OTA_STATUS_LEN && le32(sent) == OTA_ERR_NO_KEY);
    untouched();
}

// 公鑰指標是空的，或長度不是 32：當成沒有公鑰
static void no_key_bad_length(void)
{
    ctx.public_key_len = OTA_PUBLIC_KEY_LEN - 1;
    CHECK(serve() == OTA_ERR_NO_KEY);
    CHECK(le32(sent) == OTA_ERR_NO_KEY);
    untouched();
    new_connection();
    ctx.public_key = NULL;
    ctx.public_key_len = OTA_PUBLIC_KEY_LEN;
    CHECK(serve() == OTA_ERR_NO_KEY);
    untouched();
}

static void header_timeout(void)
{
    stall_at = 0;
    CHECK(serve() == OTA_ERR_TIMEOUT);
    CHECK(now_us >= OTA_T_AUTH_MS * 1000ull && now_us <= OTA_T_AUTH_MS * 1000ull + WAIT_US);
    untouched();
}

// 每 4 秒送 1 byte：簽章驗過前的期限是絕對的，5 秒到就斷
static void header_trickle(void)
{
    chunk = 1;
    gap_us = 4 * SEC;
    CHECK(serve() == OTA_ERR_TIMEOUT);
    CHECK(now_us >= OTA_T_AUTH_MS * 1000ull && now_us <= OTA_T_AUTH_MS * 1000ull + WAIT_US);
    CHECK(delivered == 2);
    untouched();
}

static void close_in_header(void)
{
    stall_at = 10;
    close_at_stall = 1;
    CHECK(serve() == OTA_ERR_IO);
    untouched();
}

// 用別把私鑰簽的：AUTH，Flash 沒動
static void wrong_key(void)
{
    sign_with = SECRET2;
    CHECK(serve() == OTA_ERR_AUTH);
    CHECK(n_sent == 2 * OTA_STATUS_LEN && le32(sent + 4) == OTA_ERR_AUTH);
    untouched();
}

// 簽完之後改了標頭（這裡是位元組總和的一個位元）：AUTH，Flash 沒動
static void header_bit_flipped(void)
{
    flip_header_bit = 1;
    CHECK(serve() == OTA_ERR_AUTH);
    CHECK(le32(sent + 4) == OTA_ERR_AUTH);
    untouched();
}

// 簽完之後改了雜湊：AUTH，Flash 沒動
static void hash_modified(void)
{
    flip_hash_byte = 1;
    CHECK(serve() == OTA_ERR_AUTH);
    CHECK(le32(sent + 4) == OTA_ERR_AUTH);
    untouched();
}

static void signature_modified(void)
{
    flip_sig_byte = 1;
    CHECK(serve() == OTA_ERR_AUTH);
    CHECK(le32(sent + 4) == OTA_ERR_AUTH);
    untouched();
}

// magic 錯但簽章對：先驗簽章、再解析標頭
static void bad_header_after_auth(void)
{
    bad_magic = 1;
    CHECK(serve() == OTA_ERR_BAD_HEADER);
    CHECK(le32(sent + 4) == OTA_ERR_BAD_HEADER);
    untouched();
}

static void too_large(void)
{
    hdr_len = SLOT_LEN;
    hashed_len = IMG_LEN;
    CHECK(serve() == OTA_ERR_TOO_LARGE);
    CHECK(le32(sent + 4) == OTA_ERR_TOO_LARGE);
    CHECK(flash_ops == 0);
    stays_on_running_slot();
}

static void armed_after_auth(void)
{
    armed_after_delivered = REQ_LEN;
    chunk = REQ_LEN;
    CHECK(serve() == OTA_ERR_ARMED);
    CHECK(le32(sent + 4) == OTA_ERR_ARMED);
    CHECK(flash_ops == 0);
    stays_on_running_slot();
}

// 簽章驗過後的 OK 送不出去：不收映像
static void status_send_fail(void)
{
    send_fail_from = OTA_STATUS_LEN;
    CHECK(serve() == OTA_ERR_IO);
    CHECK(flash_ops == 0);
    stays_on_running_slot();
}

static void armed_mid_data(void)
{
    chunk = 1024;
    armed_after_delivered = REQ_LEN + 8192;
    CHECK(serve() == OTA_ERR_ARMED);
    CHECK(le32(sent + 8) == OTA_ERR_ARMED);
    CHECK(delivered < REQ_LEN + IMG_LEN);
    stays_on_running_slot();
}

// 映像比標頭寫的短（送到一半停住）：閒置逾時，不啟用
static void idle_timeout_mid_data(void)
{
    stall_at = REQ_LEN + 5000;
    CHECK(serve() == OTA_ERR_TIMEOUT);
    CHECK(now_us >= OTA_T_IDLE_MS * 1000ull && now_us <= OTA_T_IDLE_MS * 1000ull + 2 * WAIT_US);
    CHECK(le32(sent + 8) == OTA_ERR_TIMEOUT);
    stays_on_running_slot();
}

// 映像比標頭寫的短（送到一半斷線）：不啟用
static void close_mid_data(void)
{
    stall_at = REQ_LEN + 5000;
    close_at_stall = 1;
    CHECK(serve() == OTA_ERR_IO);
    stays_on_running_slot();
}

static void flash_error_mid_data(void)
{
    fail_program_from = FW2_ADDR + 8192;
    CHECK(serve() == OTA_ERR_FLASH);
    CHECK(le32(sent + 8) == OTA_ERR_FLASH);
    stays_on_running_slot();
}

// 驗證後一直有資料進來（每 9 秒 100 bytes，不會閒置逾時），總上限到就斷
static void total_cap(void)
{
    chunk = 100;
    gap_us = 9 * SEC;
    gap_from = REQ_LEN;
    CHECK(serve() == OTA_ERR_TIMEOUT);
    CHECK(now_us >= OTA_T_TOTAL_MS * 1000ull && now_us <= OTA_T_TOTAL_MS * 1000ull + WAIT_US);
    stays_on_running_slot();
}

// 驗收：標頭、雜湊、簽章都是原本的，映像在傳輸中被改了一個位元組：
// 收完雜湊不同，BAD_IMAGE；另一槽寫過但沒有標籤與試開紀錄，模擬重開後開的還是原來的槽
static void image_byte_tampered(void)
{
    flip_image_byte = 1;
    CHECK(serve() == OTA_ERR_BAD_IMAGE);
    CHECK(le32(sent + 4) == OTA_OK && le32(sent + 8) == OTA_ERR_BAD_IMAGE);
    CHECK(flash_ops > 0);
    stays_on_running_slot();
}

// 簽過的雜湊涵蓋的內容比標頭寫的長度長（例如簽章工具和映像長度對不上）：
// 板子只收標頭寫的長度，算出的雜湊不同，BAD_IMAGE
static void signed_hash_covers_other_length(void)
{
    hdr_len = IMG_LEN - 4096;
    hashed_len = IMG_LEN;
    CHECK(serve() == OTA_ERR_BAD_IMAGE);
    CHECK(le32(sent + 8) == OTA_ERR_BAD_IMAGE);
    stays_on_running_slot();
}

// 映像與雜湊都收齊之後才解鎖：寫標籤之前的檢查擋下
static void armed_before_label(void)
{
    armed_after_delivered = REQ_LEN + IMG_LEN;
    CHECK(serve() == OTA_ERR_ARMED);
    CHECK(le32(sent + 8) == OTA_ERR_ARMED);
    stays_on_running_slot();
}

// 沒有 nonce：錄下的合法更新在下一條連線原樣重送，照樣接受（同一份簽過的映像，也就是可以降版）。
// 這是這個協定的已知性質，寫在 ota_core.h 與 SECURITY；要擋得另做版本號比對
static void replay_accepted_no_nonce(void)
{
    hdr_flags = OTA_FLAG_NO_REBOOT;
    CHECK(serve() == OTA_OK);
    installed();
    uint8_t *recorded = malloc(client_len);
    memcpy(recorded, client, client_len);
    const uint32_t recorded_len = client_len;
    // 試開確認後才能再收
    ota_slot_t slots[2];
    CHECK(ota_read_slots(&F, &LAYOUT, slots) == OTA_OK);
    CHECK(ota_confirm(&F, slots, 2) == OTA_OK);
    new_connection();
    ctx.running_idx = 2;
    replay = recorded;
    replay_len = recorded_len;
    CHECK(serve() == OTA_OK);
    CHECK(memcmp(flash + FW1_ADDR, image, IMG_LEN) == 0);
    free(recorded);
}

// 目前的槽還在試開（未確認）：拒絕，不碰 Flash；否則重試會蓋掉唯一確認過的舊槽
static void busy_while_running_on_trial(void)
{
    put_le32(flash + FW1_ADDR + SLOT_LEN - OTA_SECTOR_SIZE, OTA_TRIAL_MAGIC);
    put_le32(flash + FW1_ADDR + SLOT_LEN - OTA_SECTOR_SIZE + 4, OTA_TRIAL_PENDING);
    memcpy(snap_running, flash + FW1_ADDR, SLOT_LEN);
    CHECK(serve() == OTA_ERR_BUSY);
    CHECK(le32(sent + 4) == OTA_ERR_BUSY);
    CHECK(flash_ops == 0);
    CHECK(memcmp(flash + FW1_ADDR, snap_running, SLOT_LEN) == 0);
    for (uint32_t i = 0; i < OTA_LABEL_LEN; i++) { CHECK(flash[FW2_ADDR + i] == 0xFF); }
}

// 分割表和編進韌體的版面不同：簽章驗過後回「版面不符」，不寫 Flash
static void layout_mismatch(void)
{
    static const ota_layout_t other = {{{0x60000u, 0x400000u}, {0x520000u, 0x400000u}}, FLASH_SIZE};
    ctx.layout = &other;
    CHECK(serve() == OTA_ERR_LAYOUT);
    CHECK(le32(sent + 4) == OTA_ERR_LAYOUT);
    CHECK(flash_ops == 0);
    stays_on_running_slot();
}

// 收齊後讀回核對期間才解鎖：啟用（試開紀錄、標籤）之前的檢查擋下
static void armed_during_readback(void)
{
    armed_on_readback = 1;
    CHECK(serve() == OTA_ERR_ARMED);
    CHECK(armed_now);
    CHECK(le32(sent + 8) == OTA_ERR_ARMED);
    stays_on_running_slot();
}

// 映像已啟用但最後的狀態送不出去：照常重開（電腦端會報告安裝狀態不明）
static void final_status_send_fail_still_reboots(void)
{
    send_fail_from = 2 * OTA_STATUS_LEN;
    CHECK(serve() == OTA_OK);
    installed();
    CHECK(ctx.reboot);
}

// 試開探測：目前的槽沒有試開紀錄時回 OK 與槽號；不寫 Flash、不設旗標、不重開
static void probe_confirmed(void)
{
    probe_mode = 1;
    CHECK(serve() == OTA_OK);
    CHECK(n_sent == OTA_STATUS_LEN + OTA_PROBE_REPLY_LEN);
    CHECK(le32(sent) == OTA_OK && le32(sent + 4) == OTA_OK && le32(sent + 8) == 1);
    untouched();
}

// 換好映像、重開到 fw2 後：確認前探測回 BUSY 與槽 2，確認後回 OK；探測本身不寫 Flash
static void probe_reports_trial_then_confirmed(void)
{
    hdr_flags = OTA_FLAG_NO_REBOOT;
    CHECK(serve() == OTA_OK);
    ota_slot_t slots[2];
    uint32_t attempts = 0;
    CHECK(ota_read_slots(&F, &LAYOUT, slots) == OTA_OK);
    CHECK(ota_boot_check(&F, slots, 2, &attempts) == OTA_BOOT_TRIAL);
    new_connection();
    ctx.running_idx = 2;
    probe_mode = 1;
    CHECK(serve() == OTA_ERR_BUSY);
    CHECK(le32(sent + 4) == OTA_ERR_BUSY && le32(sent + 8) == 2);
    CHECK(flash_ops == 0 && !flag_seen && !ctx.reboot);
    CHECK(ota_confirm(&F, slots, 2) == OTA_OK);
    new_connection();
    probe_mode = 1;
    CHECK(serve() == OTA_OK);
    CHECK(le32(sent + 4) == OTA_OK && le32(sent + 8) == 2);
    CHECK(flash_ops == 0 && !flag_seen);
}

// 版面不符時探測照實回報
static void probe_layout_mismatch(void)
{
    static const ota_layout_t other = {{{0x60000u, 0x400000u}, {0x520000u, 0x400000u}}, FLASH_SIZE};
    ctx.layout = &other;
    probe_mode = 1;
    CHECK(serve() == OTA_ERR_LAYOUT);
    CHECK(le32(sent + 4) == OTA_ERR_LAYOUT);
    untouched();
}

static const struct { const char *name; void (*fn)(void); } cases[] = {
    {"success", success},
    {"progress_every_5_percent", progress_every_5_percent},
    {"success_no_reboot_flag", success_no_reboot_flag},
    {"armed_before_reboot", armed_before_reboot},
    {"flag_set_during_data", flag_set_during_data},
    {"armed_at_accept_closes_silently", armed_at_accept_closes_silently},
    {"no_key", no_key},
    {"no_key_bad_length", no_key_bad_length},
    {"header_timeout", header_timeout},
    {"header_trickle", header_trickle},
    {"close_in_header", close_in_header},
    {"wrong_key", wrong_key},
    {"header_bit_flipped", header_bit_flipped},
    {"hash_modified", hash_modified},
    {"signature_modified", signature_modified},
    {"bad_header_after_auth", bad_header_after_auth},
    {"too_large", too_large},
    {"armed_after_auth", armed_after_auth},
    {"status_send_fail", status_send_fail},
    {"armed_mid_data", armed_mid_data},
    {"idle_timeout_mid_data", idle_timeout_mid_data},
    {"close_mid_data", close_mid_data},
    {"flash_error_mid_data", flash_error_mid_data},
    {"total_cap", total_cap},
    {"image_byte_tampered", image_byte_tampered},
    {"signed_hash_covers_other_length", signed_hash_covers_other_length},
    {"armed_before_label", armed_before_label},
    {"replay_accepted_no_nonce", replay_accepted_no_nonce},
    {"busy_while_running_on_trial", busy_while_running_on_trial},
    {"armed_during_readback", armed_during_readback},
    {"final_status_send_fail_still_reboots", final_status_send_fail_still_reboots},
    {"layout_mismatch", layout_mismatch},
    {"probe_confirmed", probe_confirmed},
    {"probe_reports_trial_then_confirmed", probe_reports_trial_then_confirmed},
    {"probe_layout_mismatch", probe_layout_mismatch},
};

int main(int argc, char **argv)
{
    if (argc != 2) {
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) { printf("%s\n", cases[i].name); }
        return 2;
    }
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (strcmp(argv[1], cases[i].name) == 0) {
            setup();
            cases[i].fn();
            printf("ok\n");
            return 0;
        }
    }
    printf("unknown case %s\n", argv[1]);
    return 2;
}
