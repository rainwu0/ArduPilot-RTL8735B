/*
 * ota_core.c 的主機測試：16 MiB 的假 NOR Flash（抹除設為 0xFF、寫入只能把位元由 1 清成 0），
 * 記錄每一次抹除與寫入的位址。分割表與編進韌體的版面（LAYOUT）預設是 SDK 舊版面，個別案例會改。
 *用法：ota_core_test <案例>，通過時印 ok 並以 0 結束；不帶參數時列出案例。
 */
#include "ota_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLASH_SIZE 0x1000000u
#define FW1_ADDR 0x80000u
#define FW2_ADDR 0x4C0000u
#define SLOT_LEN 0x380000u
#define LIMIT (SLOT_LEN - OTA_SECTOR_SIZE)

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __func__, __LINE__, #c); exit(1); } } while (0)

typedef struct { char op; uint32_t addr; uint32_t len; } op_t;

static uint8_t *flash;
static op_t ops[8192];
static int n_ops;
static int nor_violation;

// 測試用的私鑰（Monocypher 的 32 bytes 種子），SECRET2 是另一把
static const uint8_t SECRET[32] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
};
static const uint8_t SECRET2[32] = {
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
};
// begin() 用的標頭
static uint8_t cur_header[OTA_HEADER_LEN];

static int fk_read(void *ctx, uint32_t a, uint8_t *b, uint32_t n)
{
    (void)ctx;
    if ((uint64_t)a + n > FLASH_SIZE) { return -1; }
    memcpy(b, flash + a, n);
    return 0;
}

static void log_op(char op, uint32_t a, uint32_t n)
{
    if (n_ops < (int)(sizeof(ops) / sizeof(ops[0]))) { ops[n_ops].op = op; ops[n_ops].addr = a; ops[n_ops].len = n; }
    n_ops++;
}

static int fk_erase(void *ctx, uint32_t a)
{
    (void)ctx;
    if (a % OTA_SECTOR_SIZE != 0 || (uint64_t)a + OTA_SECTOR_SIZE > FLASH_SIZE) { return -1; }
    memset(flash + a, 0xFF, OTA_SECTOR_SIZE);
    log_op('E', a, OTA_SECTOR_SIZE);
    return 0;
}

static int fk_program(void *ctx, uint32_t a, const uint8_t *b, uint32_t n)
{
    (void)ctx;
    if ((uint64_t)a + n > FLASH_SIZE) { return -1; }
    for (uint32_t i = 0; i < n; i++) {
        if ((flash[a + i] & b[i]) != b[i]) { nor_violation = 1; }
        flash[a + i] &= b[i];
    }
    log_op('P', a, n);
    return 0;
}

static const ota_flash_t F = {fk_read, fk_erase, fk_program, NULL};

// 編進韌體的版面與 Flash 容量；setup() 設成和分割表相同
static ota_layout_t LAYOUT;

static int read_slots(ota_slot_t slots[2])
{
    return ota_read_slots(&F, &LAYOUT, slots);
}

static void set_layout(uint32_t fw1, uint32_t fw1_len, uint32_t fw2, uint32_t fw2_len)
{
    LAYOUT.fw[0].addr = fw1;
    LAYOUT.fw[0].len = fw1_len;
    LAYOUT.fw[1].addr = fw2;
    LAYOUT.fw[1].len = fw2_len;
}

static void put_le32(uint32_t a, uint32_t v)
{
    flash[a] = (uint8_t)v; flash[a + 1] = (uint8_t)(v >> 8); flash[a + 2] = (uint8_t)(v >> 16); flash[a + 3] = (uint8_t)(v >> 24);
}

static uint32_t get_le32(uint32_t a)
{
    return (uint32_t)flash[a] | ((uint32_t)flash[a + 1] << 8) | ((uint32_t)flash[a + 2] << 16) | ((uint32_t)flash[a + 3] << 24);
}

// 分割表的兩筆槽紀錄，加上兩個「已經有效」的舊映像（標籤加一點內容）
static void setup(void)
{
    flash = malloc(FLASH_SIZE);
    memset(flash, 0xFF, FLASH_SIZE);
    put_le32(OTA_PART_FW1_RECORD, FW1_ADDR);
    put_le32(OTA_PART_FW1_RECORD + 4, SLOT_LEN);
    put_le32(OTA_PART_FW2_RECORD, FW2_ADDR);
    put_le32(OTA_PART_FW2_RECORD + 4, SLOT_LEN);
    flash[OTA_PART_FW1_RECORD + OTA_PART_VALID_OFFSET] = OTA_PART_VALID;
    flash[OTA_PART_FW2_RECORD + OTA_PART_VALID_OFFSET] = OTA_PART_VALID;
    set_layout(FW1_ADDR, SLOT_LEN, FW2_ADDR, SLOT_LEN);
    LAYOUT.flash_size = FLASH_SIZE;
    memcpy(flash + FW1_ADDR, ota_label, OTA_LABEL_LEN);
    memset(flash + FW1_ADDR + OTA_LABEL_LEN, 0x11, 64);
    memcpy(flash + FW2_ADDR, ota_label, OTA_LABEL_LEN);
    memset(flash + FW2_ADDR + OTA_LABEL_LEN, 0x22, 64);
    n_ops = 0;
    nor_violation = 0;
}

static uint8_t *make_image(uint32_t len, uint32_t *sum)
{
    uint8_t *img = malloc(len);
    uint32_t x = 12345u, s = 0;
    memcpy(img, ota_label, OTA_LABEL_LEN);
    for (uint32_t i = OTA_LABEL_LEN; i < len; i++) { x = x * 1103515245u + 12345u; img[i] = (uint8_t)(x >> 16); }
    for (uint32_t i = 0; i < len; i++) { s += img[i]; }
    *sum = s;
    return img;
}

static void make_header(uint8_t h[OTA_HEADER_LEN], uint16_t flags, uint32_t len, uint32_t sum)
{
    const uint8_t b[OTA_HEADER_LEN] = {
        'R', 'O', 'T', 'A', OTA_VERSION, 0, (uint8_t)flags, (uint8_t)(flags >> 8),
        (uint8_t)len, (uint8_t)(len >> 8), (uint8_t)(len >> 16), (uint8_t)(len >> 24),
        (uint8_t)sum, (uint8_t)(sum >> 8), (uint8_t)(sum >> 16), (uint8_t)(sum >> 24),
    };
    memcpy(h, b, sizeof(b));
}

// 電腦端的映像雜湊：BLAKE2b-512
static void image_hash(const uint8_t *img, uint32_t len, uint8_t hash[OTA_HASH_LEN])
{
    crypto_blake2b(hash, img, len);
}

// 簽章另由 auth_header 測；這裡的工作階段直接帶入「簽過的」標頭，雜湊在 finish() 時才填
// （這些案例開始時還沒有映像），所以 finish() 前把 s->expected 換成要測的值
static int begin(ota_session_t *s, const ota_slot_t slots[2], int running, uint32_t len, uint32_t sum)
{
    ota_signed_t sm;
    make_header(cur_header, 0, len, sum);
    memcpy(sm.header, cur_header, OTA_HEADER_LEN);
    memset(sm.hash, 0, OTA_HASH_LEN);
    const int r = ota_begin(s, slots, running, len, sum);
    return r ? r : ota_auth_start(s, &sm);
}

static int finish(ota_session_t *s, const uint8_t *img, uint32_t len)
{
    image_hash(img, len, s->expected);
    return ota_finish(s, &F);
}

// hash 為 NULL 時用正確的映像雜湊
static int ota_run_tag(int running, const uint8_t *img, uint32_t len, uint32_t sum, uint32_t chunk, const uint8_t *hash)
{
    ota_slot_t slots[2];
    ota_session_t s;
    int r = read_slots(slots);
    if (r) { return r; }
    r = begin(&s, slots, running, len, sum);
    if (r) { return r; }
    for (uint32_t off = 0; off < len; off += chunk) {
        const uint32_t n = len - off < chunk ? len - off : chunk;
        r = ota_write(&s, &F, img + off, n);
        if (r) { return r; }
    }
    if (hash == NULL) {
        return finish(&s, img, len);
    }
    memcpy(s.expected, hash, OTA_HASH_LEN);
    return ota_finish(&s, &F);
}

static int ota_run(int running, const uint8_t *img, uint32_t len, uint32_t sum, uint32_t chunk)
{
    return ota_run_tag(running, img, len, sum, chunk, NULL);
}

// 從第 first 個紀錄起的所有抹除與寫入是否都在 [base, base + size) 內
static int ops_within(int first, uint32_t base, uint32_t size)
{
    for (int i = first; i < n_ops; i++) {
        if (ops[i].addr < base || (uint64_t)ops[i].addr + ops[i].len > (uint64_t)base + size) { return 0; }
    }
    return 1;
}

static void label_blank(uint32_t slot)
{
    for (int i = 0; i < (int)OTA_LABEL_LEN; i++) { CHECK(flash[slot + i] == 0xFF); }
}

static void read_slots_ok(void)
{
    ota_slot_t slots[2];
    CHECK(read_slots(slots) == OTA_OK);
    CHECK(slots[0].addr == FW1_ADDR && slots[0].len == SLOT_LEN);
    CHECK(slots[1].addr == FW2_ADDR && slots[1].len == SLOT_LEN);
    CHECK(ota_image_limit(slots) == LIMIT);
}

static void read_slots_rejects_bad_tables(void)
{
    ota_slot_t slots[2];
    put_le32(OTA_PART_FW2_RECORD + 4, SLOT_LEN + 1);
    CHECK(read_slots(slots) == OTA_ERR_PARTITION);
    put_le32(OTA_PART_FW2_RECORD + 4, SLOT_LEN);
    put_le32(OTA_PART_FW2_RECORD, FW1_ADDR + OTA_SECTOR_SIZE);
    CHECK(read_slots(slots) == OTA_ERR_PARTITION);
    put_le32(OTA_PART_FW2_RECORD, 0xFFFFF000u);
    CHECK(read_slots(slots) == OTA_ERR_PARTITION);
}

static void set_table(uint32_t fw1, uint32_t fw1_len, uint32_t fw2, uint32_t fw2_len)
{
    put_le32(OTA_PART_FW1_RECORD, fw1);
    put_le32(OTA_PART_FW1_RECORD + 4, fw1_len);
    put_le32(OTA_PART_FW2_RECORD, fw2);
    put_le32(OTA_PART_FW2_RECORD + 4, fw2_len);
}

// 槽要完全落在 [0x58000, 0xF00000)：下面是開機區與分割表（所有版面的 boot_s 都在 0x57000 結束），
// 上面是 SDK 使用者資料與參數區。
// 分割表讀錯、韌體 bug 或版面改變都不能讓 OTA 抹寫到那裡。所有入口都拒絕，而且不寫 Flash
static void expect_slots_rejected(uint32_t fw1, uint32_t fw1_len, uint32_t fw2, uint32_t fw2_len)
{
    const ota_slot_t bad[2] = {{fw1, fw1_len}, {fw2, fw2_len}};
    ota_slot_t slots[2];
    ota_session_t s;
    uint32_t attempts = 99;
    set_table(fw1, fw1_len, fw2, fw2_len);
    CHECK(read_slots(slots) == OTA_ERR_PARTITION);
    // 繞過 ota_read_slots 直接給的槽也拒絕。目前槽的尾端放一筆試開中的紀錄：
    // 沒有範圍檢查時 ota_boot_check、ota_confirm 就會寫 Flash
    for (int run = 1; run <= 2; run++) {
        const uint32_t tail = bad[run - 1].addr + bad[run - 1].len - OTA_SECTOR_SIZE;
        put_le32(tail, OTA_TRIAL_MAGIC);
        put_le32(tail + 4, OTA_TRIAL_PENDING);
        CHECK(ota_begin(&s, bad, run, 1024, 0) == OTA_ERR_PARTITION);
        CHECK(ota_boot_check(&F, bad, run, &attempts) == OTA_BOOT_NORMAL);
        CHECK(ota_confirm(&F, bad, run) == OTA_ERR_STATE);
        CHECK(ota_trial_pending(&F, bad, run) == 0);
    }
    CHECK(n_ops == 0);
}

static void slot_touching_boot_area_rejected(void)
{
    expect_slots_rejected(0x57000u, SLOT_LEN, FW2_ADDR, SLOT_LEN);
}

static void slot_crossing_user_data_rejected(void)
{
    expect_slots_rejected(FW1_ADDR, SLOT_LEN, 0xE00000u, 0x200000u);
}

static void slot_in_param_area_rejected(void)
{
    expect_slots_rejected(FW1_ADDR, SLOT_LEN, 0xF80000u, 0x20000u);
}

// 剛好貼著兩端邊界的槽可以用
static void slots_at_area_edges_ok(void)
{
    ota_slot_t slots[2];
    set_table(0x58000u, SLOT_LEN, 0xF00000u - SLOT_LEN, SLOT_LEN);
    set_layout(0x58000u, SLOT_LEN, 0xF00000u - SLOT_LEN, SLOT_LEN);
    CHECK(read_slots(slots) == OTA_OK);
}

static void begin_limits(void)
{
    ota_slot_t slots[2];
    ota_session_t s;
    CHECK(read_slots(slots) == OTA_OK);
    CHECK(ota_begin(&s, slots, 1, LIMIT, 0) == OTA_OK);
    CHECK(ota_begin(&s, slots, 1, LIMIT + 1, 0) == OTA_ERR_TOO_LARGE);
    CHECK(ota_begin(&s, slots, 1, OTA_LABEL_LEN - 1, 0) == OTA_ERR_BAD_HEADER);
    CHECK(ota_begin(&s, slots, 0, 1024, 0) == OTA_ERR_STATE);
    CHECK(ota_begin(&s, slots, 3, 1024, 0) == OTA_ERR_STATE);
    CHECK(n_ops == 0);
}

static void never_touches_running_slot(void)
{
    uint32_t sum;
    uint8_t *img = make_image(LIMIT, &sum);
    CHECK(ota_run(1, img, LIMIT, sum, 4096) == OTA_OK);
    CHECK(n_ops > 0);
    CHECK(ops_within(0, FW2_ADDR, SLOT_LEN));
    CHECK(memcmp(flash + FW2_ADDR, img, LIMIT) == 0);
    CHECK(memcmp(flash + FW1_ADDR, ota_label, OTA_LABEL_LEN) == 0 && flash[FW1_ADDR + OTA_LABEL_LEN] == 0x11);
    CHECK(!nor_violation);
}

static void running_fw2_targets_fw1(void)
{
    uint32_t sum;
    uint8_t *img = make_image(100000, &sum);
    CHECK(ota_run(2, img, 100000, sum, 1000) == OTA_OK);
    CHECK(ops_within(0, FW1_ADDR, SLOT_LEN));
    CHECK(memcmp(flash + FW1_ADDR, img, 100000) == 0);
    CHECK(flash[FW2_ADDR + OTA_LABEL_LEN] == 0x22);
}

static void label_written_last(void)
{
    uint32_t sum;
    uint8_t *img = make_image(50000, &sum);
    ota_slot_t slots[2];
    ota_session_t s;
    CHECK(read_slots(slots) == OTA_OK);
    CHECK(begin(&s, slots, 1, 50000, sum) == OTA_OK);
    CHECK(ota_write(&s, &F, img, 50000) == OTA_OK);
    label_blank(FW2_ADDR);
    CHECK(memcmp(flash + FW2_ADDR + OTA_LABEL_LEN, img + OTA_LABEL_LEN, 50000 - OTA_LABEL_LEN) == 0);
    CHECK(finish(&s, img, 50000) == OTA_OK);
    CHECK(ops[n_ops - 1].op == 'P' && ops[n_ops - 1].addr == FW2_ADDR && ops[n_ops - 1].len == OTA_LABEL_LEN);
    CHECK(memcmp(flash + FW2_ADDR, ota_label, OTA_LABEL_LEN) == 0);
    const uint32_t tail = FW2_ADDR + SLOT_LEN - OTA_SECTOR_SIZE;
    CHECK(get_le32(tail) == OTA_TRIAL_MAGIC && get_le32(tail + 4) == OTA_TRIAL_PENDING && get_le32(tail + 8) == 0xFFFFFFFFu);
}

static void chunk_sizes_do_not_matter(void)
{
    uint32_t sum;
    const uint32_t len = 3 * OTA_SECTOR_SIZE + 123;
    uint8_t *img = make_image(len, &sum);
    ota_slot_t slots[2];
    ota_session_t s;
    const uint32_t sizes[] = {1, 7, 4095, 4097, 2, 100000};
    CHECK(read_slots(slots) == OTA_OK);
    CHECK(begin(&s, slots, 1, len, sum) == OTA_OK);
    uint32_t off = 0;
    for (int i = 0; off < len; i = (i + 1) % 6) {
        const uint32_t n = len - off < sizes[i] ? len - off : sizes[i];
        CHECK(ota_write(&s, &F, img + off, n) == OTA_OK);
        off += n;
    }
    CHECK(finish(&s, img, len) == OTA_OK);
    CHECK(memcmp(flash + FW2_ADDR, img, len) == 0);
    CHECK(!nor_violation);
}

static void checksum_mismatch_leaves_slot_invalid(void)
{
    uint32_t sum;
    uint8_t *img = make_image(20000, &sum);
    const uint32_t tail = FW2_ADDR + SLOT_LEN - OTA_SECTOR_SIZE;
    CHECK(ota_run(1, img, 20000, sum + 1, 4096) == OTA_ERR_CHECKSUM);
    label_blank(FW2_ADDR);
    CHECK(get_le32(tail) == 0xFFFFFFFFu);
}

static void rejects_image_without_label(void)
{
    uint32_t sum;
    uint8_t *img = make_image(20000, &sum);
    img[0] = 'X';
    sum = sum - 'R' + 'X';
    CHECK(ota_run(1, img, 20000, sum, 4096) == OTA_ERR_BAD_IMAGE);
    label_blank(FW2_ADDR);
}

static void write_past_image_len_rejected(void)
{
    uint32_t sum;
    uint8_t *img = make_image(10000, &sum);
    ota_slot_t slots[2];
    ota_session_t s;
    CHECK(read_slots(slots) == OTA_OK);
    CHECK(begin(&s, slots, 1, 9000, sum) == OTA_OK);
    CHECK(ota_write(&s, &F, img, 10000) == OTA_ERR_RANGE);
    CHECK(ota_write(&s, &F, img, 10) == OTA_ERR_STATE);
    CHECK(finish(&s, img, 9000) == OTA_ERR_STATE);
}

static void range_allowed_edges(void)
{
    ota_slot_t slots[2];
    ota_session_t s;
    CHECK(read_slots(slots) == OTA_OK);
    CHECK(begin(&s, slots, 1, 1024, 0) == OTA_OK);
    CHECK(ota_range_allowed(&s, FW2_ADDR, SLOT_LEN));
    CHECK(ota_range_allowed(&s, FW2_ADDR + SLOT_LEN - 1, 1));
    CHECK(!ota_range_allowed(&s, FW2_ADDR + SLOT_LEN - 1, 2));
    CHECK(!ota_range_allowed(&s, FW2_ADDR - 1, 2));
    CHECK(!ota_range_allowed(&s, FW2_ADDR, 0));
    CHECK(!ota_range_allowed(&s, FW1_ADDR, 1));
    CHECK(!ota_range_allowed(&s, 0xFFFFF000u, 0x2000u));
    CHECK(!ota_range_allowed(&s, FW2_ADDR + 16, 0xFFFFFFF0u));
    s.active = 0;
    CHECK(!ota_range_allowed(&s, FW2_ADDR, 16));
}

static void partial_then_new_session(void)
{
    uint32_t sum;
    uint8_t *img = make_image(30000, &sum);
    ota_slot_t slots[2];
    ota_session_t s;
    CHECK(read_slots(slots) == OTA_OK);
    CHECK(begin(&s, slots, 1, 30000, sum) == OTA_OK);
    CHECK(ota_write(&s, &F, img, 5000) == OTA_OK);
    // 傳輸中斷：另一槽的舊標籤已在第一次抹除時清掉，新標籤還沒寫
    label_blank(FW2_ADDR);
    CHECK(!ota_label_valid(&F, &slots[1]));
    CHECK(ota_run(1, img, 30000, sum, 4096) == OTA_OK);
    CHECK(ota_label_valid(&F, &slots[1]));
}

static void trial_boots_then_rollback(void)
{
    uint32_t sum, attempts = 0;
    uint8_t *img = make_image(40000, &sum);
    ota_slot_t slots[2];
    uint8_t sector0[OTA_SECTOR_SIZE];
    CHECK(ota_run(1, img, 40000, sum, 4096) == OTA_OK);
    CHECK(read_slots(slots) == OTA_OK);
    memcpy(sector0, flash + FW2_ADDR, sizeof(sector0));
    for (uint32_t k = 1; k <= OTA_TRIAL_MAX_ATTEMPTS; k++) {
        const int first = n_ops;
        CHECK(ota_boot_check(&F, slots, 2, &attempts) == OTA_BOOT_TRIAL);
        CHECK(attempts == k);
        CHECK(ops_within(first, FW2_ADDR + SLOT_LEN - OTA_SECTOR_SIZE, OTA_SECTOR_SIZE));
    }
    const int first = n_ops;
    CHECK(ota_boot_check(&F, slots, 2, &attempts) == OTA_BOOT_ROLLBACK);
    CHECK(attempts == OTA_TRIAL_MAX_ATTEMPTS);
    CHECK(ops_within(first, FW2_ADDR, OTA_LABEL_LEN));
    for (int i = 0; i < (int)OTA_LABEL_LEN; i++) { CHECK(flash[FW2_ADDR + i] == 0); }
    CHECK(memcmp(flash + FW2_ADDR + OTA_LABEL_LEN, sector0 + OTA_LABEL_LEN, sizeof(sector0) - OTA_LABEL_LEN) == 0);
    CHECK(ota_label_valid(&F, &slots[0]));
    CHECK(!nor_violation);
}

static void no_fallback_when_other_slot_invalid(void)
{
    uint32_t sum, attempts = 0;
    uint8_t *img = make_image(40000, &sum);
    ota_slot_t slots[2];
    CHECK(ota_run(1, img, 40000, sum, 4096) == OTA_OK);
    CHECK(read_slots(slots) == OTA_OK);
    memset(flash + FW1_ADDR, 0x00, OTA_LABEL_LEN);
    for (uint32_t k = 1; k <= OTA_TRIAL_MAX_ATTEMPTS; k++) {
        CHECK(ota_boot_check(&F, slots, 2, &attempts) == OTA_BOOT_TRIAL);
    }
    CHECK(ota_boot_check(&F, slots, 2, &attempts) == OTA_BOOT_NO_FALLBACK);
    CHECK(memcmp(flash + FW2_ADDR, ota_label, OTA_LABEL_LEN) == 0);
}

static void confirm_ends_trial(void)
{
    uint32_t sum, attempts = 0;
    uint8_t *img = make_image(40000, &sum);
    ota_slot_t slots[2];
    CHECK(ota_run(1, img, 40000, sum, 4096) == OTA_OK);
    CHECK(read_slots(slots) == OTA_OK);
    CHECK(ota_boot_check(&F, slots, 2, &attempts) == OTA_BOOT_TRIAL);
    CHECK(ota_confirm(&F, slots, 2) == OTA_OK);
    CHECK(get_le32(FW2_ADDR + SLOT_LEN - OTA_SECTOR_SIZE + 4) == OTA_TRIAL_CONFIRMED);
    const int first = n_ops;
    CHECK(ota_confirm(&F, slots, 2) == OTA_OK);
    CHECK(ota_boot_check(&F, slots, 2, &attempts) == OTA_BOOT_NORMAL);
    CHECK(n_ops == first);
    CHECK(!nor_violation);
}

// 確認成功之後，之後的每次開機與確認都不碰 Flash
static void confirmed_boot_writes_nothing(void)
{
    uint32_t sum, attempts = 0;
    uint8_t *img = make_image(40000, &sum);
    ota_slot_t slots[2];
    CHECK(ota_run(1, img, 40000, sum, 4096) == OTA_OK);
    CHECK(read_slots(slots) == OTA_OK);
    CHECK(ota_boot_check(&F, slots, 2, &attempts) == OTA_BOOT_TRIAL);
    CHECK(ota_confirm(&F, slots, 2) == OTA_OK);
    const int first = n_ops;
    for (int i = 0; i < 10; i++) {
        attempts = 99;
        CHECK(ota_boot_check(&F, slots, 2, &attempts) == OTA_BOOT_NORMAL);
        CHECK(attempts == 0);
        CHECK(ota_confirm(&F, slots, 2) == OTA_OK);
    }
    CHECK(n_ops == first);
}

static void no_record_means_normal_boot(void)
{
    uint32_t attempts = 99;
    ota_slot_t slots[2];
    CHECK(read_slots(slots) == OTA_OK);
    const int first = n_ops;
    CHECK(ota_boot_check(&F, slots, 1, &attempts) == OTA_BOOT_NORMAL);
    CHECK(ota_boot_check(&F, slots, 0, &attempts) == OTA_BOOT_NORMAL);
    CHECK(ota_confirm(&F, slots, 1) == OTA_OK);
    CHECK(n_ops == first);
}

static void header_parse(void)
{
    uint8_t b[OTA_HEADER_LEN] = {'R', 'O', 'T', 'A', 2, 0, 1, 0, 0x10, 0x27, 0, 0, 0x78, 0x56, 0x34, 0x12};
    ota_header_t h;
    CHECK(ota_parse_header(b, &h) == OTA_OK);
    CHECK(h.flags == OTA_FLAG_NO_REBOOT && h.image_len == 10000 && h.checksum == 0x12345678u);
    b[0] = 'X';
    CHECK(ota_parse_header(b, &h) == OTA_ERR_BAD_HEADER);
    b[0] = 'R';
    // 版本不是 2 的標頭都拒絕
    b[4] = 1;
    CHECK(ota_parse_header(b, &h) == OTA_ERR_BAD_HEADER);
    b[4] = 3;
    CHECK(ota_parse_header(b, &h) == OTA_ERR_BAD_HEADER);
    b[4] = 2;
    b[8] = 7;
    b[9] = 0;
    CHECK(ota_parse_header(b, &h) == OTA_ERR_BAD_HEADER);
}

// 簽章涵蓋標頭與雜湊：改任何一個位元、換一把私鑰簽都不通過；沒有公鑰或長度不對回 NO_KEY。都不碰 Flash
static void auth_header(void)
{
    uint8_t pk[OTA_PUBLIC_KEY_LEN], msg[OTA_AUTH_MSG_LEN], sig[OTA_SIG_LEN], bad[OTA_SIG_LEN];
    ota_signed_t out;
    crypto_sign_public_key(pk, SECRET);
    make_header(msg, 0, 10000, 1234);
    for (uint32_t i = 0; i < OTA_HASH_LEN; i++) { msg[OTA_HEADER_LEN + i] = (uint8_t)(i * 7 + 3); }
    crypto_sign(sig, SECRET, pk, msg, sizeof(msg));
    memset(&out, 0, sizeof(out));
    CHECK(ota_auth_header(pk, sizeof(pk), msg, sig, &out) == OTA_OK);
    CHECK(memcmp(out.header, msg, OTA_HEADER_LEN) == 0 && memcmp(out.hash, msg + OTA_HEADER_LEN, OTA_HASH_LEN) == 0);
    // 每個位元都在簽章範圍內
    for (uint32_t i = 0; i < OTA_AUTH_MSG_LEN * 8; i++) {
        msg[i / 8] ^= (uint8_t)(1u << (i % 8));
        CHECK(ota_auth_header(pk, sizeof(pk), msg, sig, &out) == OTA_ERR_AUTH);
        msg[i / 8] ^= (uint8_t)(1u << (i % 8));
    }
    memcpy(bad, sig, sizeof(bad));
    bad[0] ^= 0x01;
    CHECK(ota_auth_header(pk, sizeof(pk), msg, bad, &out) == OTA_ERR_AUTH);
    bad[0] ^= 0x01;
    bad[OTA_SIG_LEN - 1] ^= 0x80;
    CHECK(ota_auth_header(pk, sizeof(pk), msg, bad, &out) == OTA_ERR_AUTH);
    // 別把私鑰簽的
    crypto_sign(bad, SECRET2, NULL, msg, sizeof(msg));
    CHECK(ota_auth_header(pk, sizeof(pk), msg, bad, &out) == OTA_ERR_AUTH);
    // 沒有公鑰、長度不對、全 0 的公鑰
    static const uint8_t zero[OTA_PUBLIC_KEY_LEN] = {0};
    CHECK(ota_auth_header(pk, 0, msg, sig, &out) == OTA_ERR_NO_KEY);
    CHECK(ota_auth_header(NULL, OTA_PUBLIC_KEY_LEN, msg, sig, &out) == OTA_ERR_NO_KEY);
    CHECK(ota_auth_header(pk, OTA_PUBLIC_KEY_LEN - 1, msg, sig, &out) == OTA_ERR_NO_KEY);
    CHECK(ota_auth_header(zero, sizeof(zero), msg, sig, &out) == OTA_ERR_AUTH);
    CHECK(n_ops == 0);
}

// ota_begin 之後沒有 ota_auth_start 就不能寫
static void write_before_auth_rejected(void)
{
    uint32_t sum;
    uint8_t *img = make_image(10000, &sum);
    ota_slot_t slots[2];
    ota_session_t s;
    CHECK(read_slots(slots) == OTA_OK);
    CHECK(ota_begin(&s, slots, 1, 10000, sum) == OTA_OK);
    CHECK(ota_write(&s, &F, img, 100) == OTA_ERR_STATE);
    CHECK(ota_finish(&s, &F) == OTA_ERR_STATE);
    CHECK(n_ops == 0);
}

// 收到的映像和簽過的雜湊不同：BAD_IMAGE，另一槽沒有標籤與試開紀錄
static void bad_image_hash_leaves_slot_invalid(void)
{
    uint32_t sum;
    uint8_t *img = make_image(20000, &sum);
    uint8_t hash[OTA_HASH_LEN] = {0};
    const uint32_t tail = FW2_ADDR + SLOT_LEN - OTA_SECTOR_SIZE;
    CHECK(ota_run_tag(1, img, 20000, sum, 4096, hash) == OTA_ERR_BAD_IMAGE);
    label_blank(FW2_ADDR);
    CHECK(get_le32(tail) == 0xFFFFFFFFu);
}

// 傳輸中被改過的映像（位元組總和也配合改過）：雜湊依原本的映像算，不通過
static void tampered_image_rejected(void)
{
    uint32_t sum;
    uint8_t *img = make_image(20000, &sum);
    uint8_t hash[OTA_HASH_LEN];
    image_hash(img, 20000, hash);
    img[10000] ^= 0x01;
    img[10001] ^= 0x01;
    const uint32_t tampered_sum = sum - (img[10000] ^ 0x01) - (img[10001] ^ 0x01) + img[10000] + img[10001];
    CHECK(ota_run_tag(1, img, 20000, tampered_sum, 4096, hash) == OTA_ERR_BAD_IMAGE);
    label_blank(FW2_ADDR);
}

// 中途放棄：工作階段結束，之後不能再寫或完成
static void abort_ends_session(void)
{
    uint32_t sum;
    uint8_t *img = make_image(10000, &sum);
    ota_slot_t slots[2];
    ota_session_t s;
    CHECK(read_slots(slots) == OTA_OK);
    CHECK(begin(&s, slots, 1, 10000, sum) == OTA_OK);
    CHECK(ota_write(&s, &F, img, 5000) == OTA_OK);
    const int first = n_ops;
    ota_abort(&s);
    CHECK(!s.active && !s.authed);
    CHECK(ota_write(&s, &F, img + 5000, 5000) == OTA_ERR_STATE);
    CHECK(finish(&s, img, 10000) == OTA_ERR_STATE);
    CHECK(n_ops == first);
    label_blank(FW2_ADDR);
}

// 目前的槽是否在試開期間：沒有紀錄或已確認時不是；讀取不寫 Flash
static void trial_pending_states(void)
{
    uint32_t sum, attempts = 0;
    uint8_t *img = make_image(40000, &sum);
    ota_slot_t slots[2];
    CHECK(read_slots(slots) == OTA_OK);
    CHECK(ota_trial_pending(&F, slots, 1) == 0);
    CHECK(ota_run(1, img, 40000, sum, 4096) == OTA_OK);
    CHECK(ota_trial_pending(&F, slots, 2) == 1);
    CHECK(ota_boot_check(&F, slots, 2, &attempts) == OTA_BOOT_TRIAL);
    CHECK(ota_trial_pending(&F, slots, 2) == 1);
    CHECK(ota_confirm(&F, slots, 2) == OTA_OK);
    const int first = n_ops;
    CHECK(ota_trial_pending(&F, slots, 2) == 0);
    CHECK(ota_trial_pending(&F, slots, 1) == 0);
    CHECK(n_ops == first);
}

// 核對（雜湊、標籤、總和）與啟用（試開紀錄、標籤）分開，中間可以再檢查解鎖
static void verify_then_activate(void)
{
    uint32_t sum;
    uint8_t *img = make_image(30000, &sum);
    ota_slot_t slots[2];
    ota_session_t s;
    const uint32_t tail = FW2_ADDR + SLOT_LEN - OTA_SECTOR_SIZE;
    CHECK(read_slots(slots) == OTA_OK);
    CHECK(begin(&s, slots, 1, 30000, sum) == OTA_OK);
    CHECK(ota_write(&s, &F, img, 30000) == OTA_OK);
    CHECK(ota_activate(&s, &F) == OTA_ERR_STATE);
    CHECK(!s.active);
    label_blank(FW2_ADDR);

    CHECK(begin(&s, slots, 1, 30000, sum) == OTA_OK);
    CHECK(ota_write(&s, &F, img, 30000) == OTA_OK);
    image_hash(img, 30000, s.expected);
    const int first = n_ops;
    CHECK(ota_verify(&s, &F) == OTA_OK);
    CHECK(n_ops == first);
    label_blank(FW2_ADDR);
    CHECK(get_le32(tail) == 0xFFFFFFFFu);
    CHECK(ota_activate(&s, &F) == OTA_OK);
    CHECK(memcmp(flash + FW2_ADDR, ota_label, OTA_LABEL_LEN) == 0);
    CHECK(get_le32(tail) == OTA_TRIAL_MAGIC);
    CHECK(!s.active);
    CHECK(ota_activate(&s, &F) == OTA_ERR_STATE);
}

// 試開中的映像何時確認：開機滿時間、接收端可用（能再收下一次更新）、主迴圈健康、Wi-Fi 連上，缺一不可
static void confirm_ready_rules(void)
{
    CHECK(ota_confirm_ready(OTA_CONFIRM_AFTER_MS, 1, 1, 1));
    CHECK(ota_confirm_ready(OTA_CONFIRM_AFTER_MS + 100000u, 1, 1, 1));
    CHECK(!ota_confirm_ready(OTA_CONFIRM_AFTER_MS - 1, 1, 1, 1));
    CHECK(!ota_confirm_ready(OTA_CONFIRM_AFTER_MS, 0, 1, 1));
    CHECK(!ota_confirm_ready(OTA_CONFIRM_AFTER_MS, 1, 0, 1));
    CHECK(!ota_confirm_ready(OTA_CONFIRM_AFTER_MS, 1, 1, 0));
}

// 兩槽不等長、fw1 在 fw2 之後：上限取較短者，寫入只在另一槽內
static void unequal_reversed_slots(void)
{
    const uint32_t fw1 = 0x500000u, fw1_len = 0x300000u, fw2 = 0x80000u, fw2_len = 0x400000u;
    uint32_t sum, attempts = 0;
    put_le32(OTA_PART_FW1_RECORD, fw1);
    put_le32(OTA_PART_FW1_RECORD + 4, fw1_len);
    put_le32(OTA_PART_FW2_RECORD, fw2);
    put_le32(OTA_PART_FW2_RECORD + 4, fw2_len);
    memcpy(flash + fw1, ota_label, OTA_LABEL_LEN);
    set_layout(fw1, fw1_len, fw2, fw2_len);
    ota_slot_t slots[2];
    CHECK(read_slots(slots) == OTA_OK);
    CHECK(ota_image_limit(slots) == fw1_len - OTA_SECTOR_SIZE);
    uint8_t *img = make_image(100000, &sum);
    CHECK(ota_run(1, img, 100000, sum, 3000) == OTA_OK);
    CHECK(ops_within(0, fw2, fw2_len));
    CHECK(memcmp(flash + fw2, img, 100000) == 0);
    CHECK(get_le32(fw2 + fw2_len - OTA_SECTOR_SIZE) == OTA_TRIAL_MAGIC);
    CHECK(ota_boot_check(&F, slots, 2, &attempts) == OTA_BOOT_TRIAL);
    CHECK(ota_run(2, img, fw1_len - OTA_SECTOR_SIZE + 1, sum, 4096) == OTA_ERR_TOO_LARGE);
}

// realtek-ota 版面：fw1 0x60000、fw2 0x520000，各 4 MiB
static void read_slots_realtek_ota_layout(void)
{
    ota_slot_t slots[2];
    set_table(0x60000u, 0x400000u, 0x520000u, 0x400000u);
    set_layout(0x60000u, 0x400000u, 0x520000u, 0x400000u);
    CHECK(read_slots(slots) == OTA_OK);
    CHECK(ota_image_limit(slots) == 0x400000u - OTA_SECTOR_SIZE);
}

// 分割表和編進韌體的版面不同：拒絕，不寫 Flash；slots 仍是分割表的記錄
static void read_slots_rejects_layout_mismatch(void)
{
    ota_slot_t slots[2];
    set_layout(0x60000u, 0x400000u, 0x520000u, 0x400000u);
    CHECK(read_slots(slots) == OTA_ERR_LAYOUT);
    CHECK(slots[0].addr == FW1_ADDR && slots[0].len == SLOT_LEN);
    CHECK(slots[1].addr == FW2_ADDR && slots[1].len == SLOT_LEN);
    set_layout(FW1_ADDR, SLOT_LEN, FW2_ADDR, SLOT_LEN + OTA_SECTOR_SIZE);
    CHECK(read_slots(slots) == OTA_ERR_LAYOUT);
    set_layout(FW2_ADDR, SLOT_LEN, FW1_ADDR, SLOT_LEN);
    CHECK(read_slots(slots) == OTA_ERR_LAYOUT);
    CHECK(slots[0].addr == FW1_ADDR && slots[1].addr == FW2_ADDR);
    CHECK(n_ops == 0);
}

// 槽要整個在 Flash 的實際容量內；容量讀不到（0）一律不在，位址加長度溢位也不在
static void slots_fit_rules(void)
{
    ota_slot_t slots[2] = {{FW1_ADDR, SLOT_LEN}, {FW2_ADDR, SLOT_LEN}};
    CHECK(ota_slots_fit(slots, FLASH_SIZE));
    CHECK(ota_slots_fit(slots, FW2_ADDR + SLOT_LEN));
    CHECK(!ota_slots_fit(slots, FW2_ADDR + SLOT_LEN - 1));
    CHECK(!ota_slots_fit(slots, 0));
    slots[0].addr = 0xFFFFF000u;
    slots[0].len = 0x2000u;
    CHECK(!ota_slots_fit(slots, 0xFFFFFFFFu));
}

// 版面不同的映像經舊接收端寫進另一槽：開機檢查照分割表記錄試開，用完次數後退回，不會卡在試開。
// 分割表放不進 Flash 時 slots 不能用（寫到容量之外會繞回 Flash 開頭）
static void layout_mismatch_trial_falls_back(void)
{
    uint32_t sum, attempts = 0;
    uint8_t *img = make_image(40000, &sum);
    ota_slot_t slots[2];
    CHECK(ota_run(1, img, 40000, sum, 4096) == OTA_OK);
    set_layout(0x60000u, 0x400000u, 0x520000u, 0x400000u);
    CHECK(read_slots(slots) == OTA_ERR_LAYOUT);
    CHECK(ota_slots_fit(slots, LAYOUT.flash_size));
    for (uint32_t k = 1; k <= OTA_TRIAL_MAX_ATTEMPTS; k++) {
        const int first = n_ops;
        CHECK(ota_boot_check(&F, slots, 2, &attempts) == OTA_BOOT_TRIAL);
        CHECK(attempts == k);
        CHECK(ops_within(first, FW2_ADDR + SLOT_LEN - OTA_SECTOR_SIZE, OTA_SECTOR_SIZE));
    }
    CHECK(ota_boot_check(&F, slots, 2, &attempts) == OTA_BOOT_ROLLBACK);
    CHECK(ota_label_valid(&F, &slots[0]) && !ota_label_valid(&F, &slots[1]));
    LAYOUT.flash_size = FW2_ADDR;
    CHECK(read_slots(slots) == OTA_ERR_LAYOUT);
    CHECK(!ota_slots_fit(slots, LAYOUT.flash_size));
    CHECK(!nor_violation);
}

// 開機時怎麼處理試開（ota_app.c 照 ota_boot_plan() 做）
static void boot_plan_rules(void)
{
    ota_slot_t slots[2];
    CHECK(read_slots(slots) == OTA_OK);
    CHECK(ota_boot_plan(OTA_OK, slots, FLASH_SIZE) == OTA_PLAN_CHECK);
    CHECK(ota_boot_plan(OTA_ERR_PARTITION, slots, FLASH_SIZE) == OTA_PLAN_SKIP);
    CHECK(ota_boot_plan(OTA_ERR_FLASH, slots, FLASH_SIZE) == OTA_PLAN_SKIP);
    // 版面不同：槽放得進 Flash 時照分割表記錄試開但不確認，放不進時只讀不寫
    CHECK(ota_boot_plan(OTA_ERR_LAYOUT, slots, FLASH_SIZE) == OTA_PLAN_CHECK_NO_CONFIRM);
    CHECK(ota_boot_plan(OTA_ERR_LAYOUT, slots, FW2_ADDR + SLOT_LEN) == OTA_PLAN_CHECK_NO_CONFIRM);
    CHECK(ota_boot_plan(OTA_ERR_LAYOUT, slots, FW2_ADDR + SLOT_LEN - 1) == OTA_PLAN_BLOCK_ONLY);
    CHECK(ota_boot_plan(OTA_ERR_LAYOUT, slots, 0) == OTA_PLAN_BLOCK_ONLY);
    CHECK(n_ops == 0);
}

// 開機檢查讀不到 JEDEC ID（0）時假設 16 MiB：版面相同時照常檢查試開，不能被當成版面不符而卡在試開
static void capacity_unknown_assumes_16mib(void)
{
    ota_slot_t slots[2];
    uint32_t sum, attempts = 0;
    uint8_t *img = make_image(40000, &sum);
    CHECK(ota_flash_capacity(0) == OTA_FLASH_ASSUMED_SIZE);
    CHECK(OTA_FLASH_ASSUMED_SIZE == FLASH_SIZE);
    CHECK(ota_flash_capacity(0x800000u) == 0x800000u);
    CHECK(ota_run(1, img, 40000, sum, 4096) == OTA_OK);
    LAYOUT.flash_size = ota_flash_capacity(0);
    const int r = read_slots(slots);
    CHECK(r == OTA_OK);
    CHECK(ota_boot_plan(r, slots, LAYOUT.flash_size) == OTA_PLAN_CHECK);
    CHECK(ota_boot_check(&F, slots, 2, &attempts) == OTA_BOOT_TRIAL);
    // 對照：容量當成 0 直接比，版面相同也會變成不符
    LAYOUT.flash_size = 0;
    CHECK(read_slots(slots) == OTA_ERR_LAYOUT);
}

// 每筆記錄最後一個 byte 是 valid（建出的 flash_ntz.bin 為 0x01）
static void read_slots_rejects_invalid_record(void)
{
    ota_slot_t slots[2];
    flash[OTA_PART_FW2_RECORD + OTA_PART_VALID_OFFSET] = 0x00;
    CHECK(read_slots(slots) == OTA_ERR_PARTITION);
    flash[OTA_PART_FW2_RECORD + OTA_PART_VALID_OFFSET] = 0xFF;
    CHECK(read_slots(slots) == OTA_ERR_PARTITION);
    flash[OTA_PART_FW2_RECORD + OTA_PART_VALID_OFFSET] = OTA_PART_VALID;
    flash[OTA_PART_FW1_RECORD + OTA_PART_VALID_OFFSET] = 0x00;
    CHECK(read_slots(slots) == OTA_ERR_PARTITION);
    CHECK(n_ops == 0);
}

// 讀不到容量（0）或槽超出實際容量：拒絕（較小的 Flash 位址可能繞回開機區）
static void read_slots_checks_capacity(void)
{
    ota_slot_t slots[2];
    LAYOUT.flash_size = 0;
    CHECK(read_slots(slots) == OTA_ERR_LAYOUT);
    LAYOUT.flash_size = 0x800000u;
    CHECK(read_slots(slots) == OTA_ERR_LAYOUT);
    LAYOUT.flash_size = FW2_ADDR + SLOT_LEN;
    CHECK(read_slots(slots) == OTA_OK);
}

// 試開逾時：未解鎖時到期就重開；解鎖中絕不重開，上鎖後再等寬限才重開；已確認永不重開
static void trial_timeout_rules(void)
{
    const uint32_t N = OTA_TRIAL_TIMEOUT_MS, G = OTA_TRIAL_DISARM_GRACE_MS;
    ota_trial_timer_t t;
    memset(&t, 0, sizeof(t));
    CHECK(!ota_trial_timeout_due(&t, N - 1, 0, 0));
    CHECK(ota_trial_timeout_due(&t, N, 0, 0));
    memset(&t, 0, sizeof(t));
    CHECK(!ota_trial_timeout_due(&t, N * 10, 1, 0));
    // 到期時解鎖中：一直延後；上鎖後未滿寬限不重開，滿了才重開
    memset(&t, 0, sizeof(t));
    CHECK(!ota_trial_timeout_due(&t, N, 0, 1));
    CHECK(!ota_trial_timeout_due(&t, N * 3, 0, 1));
    CHECK(!ota_trial_timeout_due(&t, N * 3 + 1000, 0, 0));
    CHECK(!ota_trial_timeout_due(&t, N * 3 + 1000 + G - 1, 0, 0));
    CHECK(ota_trial_timeout_due(&t, N * 3 + 1000 + G, 0, 0));
    // 寬限中又解鎖：下次上鎖起重新等
    memset(&t, 0, sizeof(t));
    CHECK(!ota_trial_timeout_due(&t, N - 5000, 0, 1));
    CHECK(!ota_trial_timeout_due(&t, N, 0, 0));
    CHECK(!ota_trial_timeout_due(&t, N + 1000, 0, 1));
    CHECK(!ota_trial_timeout_due(&t, N + 2000, 0, 0));
    CHECK(!ota_trial_timeout_due(&t, N + 2000 + G - 1, 0, 0));
    CHECK(ota_trial_timeout_due(&t, N + 2000 + G, 0, 0));
    // 到期前解鎖又上鎖：到期時仍要等滿上鎖後的寬限
    memset(&t, 0, sizeof(t));
    CHECK(!ota_trial_timeout_due(&t, 1000, 0, 1));
    CHECK(!ota_trial_timeout_due(&t, N - 10, 0, 0));
    CHECK(!ota_trial_timeout_due(&t, N, 0, 0));
    CHECK(ota_trial_timeout_due(&t, N - 10 + G, 0, 0));
}

static const struct { const char *name; void (*fn)(void); } cases[] = {
    {"read_slots_ok", read_slots_ok},
    {"read_slots_rejects_bad_tables", read_slots_rejects_bad_tables},
    {"slot_touching_boot_area_rejected", slot_touching_boot_area_rejected},
    {"slot_crossing_user_data_rejected", slot_crossing_user_data_rejected},
    {"slot_in_param_area_rejected", slot_in_param_area_rejected},
    {"slots_at_area_edges_ok", slots_at_area_edges_ok},
    {"begin_limits", begin_limits},
    {"never_touches_running_slot", never_touches_running_slot},
    {"running_fw2_targets_fw1", running_fw2_targets_fw1},
    {"label_written_last", label_written_last},
    {"chunk_sizes_do_not_matter", chunk_sizes_do_not_matter},
    {"checksum_mismatch_leaves_slot_invalid", checksum_mismatch_leaves_slot_invalid},
    {"rejects_image_without_label", rejects_image_without_label},
    {"write_past_image_len_rejected", write_past_image_len_rejected},
    {"range_allowed_edges", range_allowed_edges},
    {"partial_then_new_session", partial_then_new_session},
    {"trial_boots_then_rollback", trial_boots_then_rollback},
    {"no_fallback_when_other_slot_invalid", no_fallback_when_other_slot_invalid},
    {"confirm_ends_trial", confirm_ends_trial},
    {"confirmed_boot_writes_nothing", confirmed_boot_writes_nothing},
    {"no_record_means_normal_boot", no_record_means_normal_boot},
    {"header_parse", header_parse},
    {"auth_header", auth_header},
    {"write_before_auth_rejected", write_before_auth_rejected},
    {"bad_image_hash_leaves_slot_invalid", bad_image_hash_leaves_slot_invalid},
    {"tampered_image_rejected", tampered_image_rejected},
    {"abort_ends_session", abort_ends_session},
    {"trial_pending_states", trial_pending_states},
    {"verify_then_activate", verify_then_activate},
    {"confirm_ready_rules", confirm_ready_rules},
    {"unequal_reversed_slots", unequal_reversed_slots},
    {"read_slots_realtek_ota_layout", read_slots_realtek_ota_layout},
    {"read_slots_rejects_layout_mismatch", read_slots_rejects_layout_mismatch},
    {"read_slots_rejects_invalid_record", read_slots_rejects_invalid_record},
    {"read_slots_checks_capacity", read_slots_checks_capacity},
    {"slots_fit_rules", slots_fit_rules},
    {"layout_mismatch_trial_falls_back", layout_mismatch_trial_falls_back},
    {"boot_plan_rules", boot_plan_rules},
    {"capacity_unknown_assumes_16mib", capacity_unknown_assumes_16mib},
    {"trial_timeout_rules", trial_timeout_rules},
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
