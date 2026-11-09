/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Wi-Fi 韌體更新（OTA）的核心：公鑰簽章、BLAKE2b 雜湊、Flash 寫入與試開，規則見 ota_core.h。
 */

#include "ota_core.h"

#include <stddef.h>
#include <string.h>

const uint8_t ota_label[OTA_LABEL_LEN] = {'R', 'T', 'L', '8', '7', '3', '5', 'B'};

static uint32_t _le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void _put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

// [addr, addr + len) 完全落在 [base, base + size) 內；不會溢位
static int _within(uint32_t addr, uint32_t len, uint32_t base, uint32_t size)
{
    return addr >= base && addr - base <= size && len <= size - (addr - base);
}

static int _overlaps(uint32_t addr, uint32_t len, uint32_t base, uint32_t size)
{
    const uint64_t a0 = addr, a1 = (uint64_t)addr + len, b0 = base, b1 = (uint64_t)base + size;
    return len > 0 && size > 0 && a0 < b1 && b0 < a1;
}

static int _slot_ok(const ota_slot_t *s)
{
    return s->len >= 2u * OTA_SECTOR_SIZE && s->len % OTA_SECTOR_SIZE == 0 && s->addr % OTA_SECTOR_SIZE == 0 &&
           _within(s->addr, s->len, OTA_SLOT_FLOOR, OTA_SLOT_CEILING - OTA_SLOT_FLOOR);
}

static int _slots_ok(const ota_slot_t slots[2])
{
    return _slot_ok(&slots[0]) && _slot_ok(&slots[1]) &&
           !_overlaps(slots[0].addr, slots[0].len, slots[1].addr, slots[1].len);
}

static uint32_t _trial_addr(const ota_slot_t *s)
{
    return s->addr + s->len - OTA_SECTOR_SIZE;
}

int ota_read_slots(const ota_flash_t *f, const ota_layout_t *layout, ota_slot_t slots[2])
{
    static const uint32_t records[2] = {OTA_PART_FW1_RECORD, OTA_PART_FW2_RECORD};
    uint8_t rec[OTA_PART_RECORD_LEN];
    int valid = 1;
    for (int i = 0; i < 2; i++) {
        if (f->read(f->ctx, records[i], rec, sizeof(rec)) != 0) {
            return OTA_ERR_FLASH;
        }
        slots[i].addr = _le32(rec);
        slots[i].len = _le32(rec + 4);
        valid = valid && rec[OTA_PART_VALID_OFFSET] == OTA_PART_VALID;
    }
    if (!valid || !_slots_ok(slots)) {
        return OTA_ERR_PARTITION;
    }
    // OTA 不能改分割表：版面不同的韌體寫進另一槽後，會拒絕自己的 OTA 而無法確認
    for (int i = 0; i < 2; i++) {
        if (slots[i].addr != layout->fw[i].addr || slots[i].len != layout->fw[i].len) {
            return OTA_ERR_LAYOUT;
        }
    }
    return ota_slots_fit(slots, layout->flash_size) ? OTA_OK : OTA_ERR_LAYOUT;
}

int ota_slots_fit(const ota_slot_t slots[2], uint32_t flash_size)
{
    return _within(slots[0].addr, slots[0].len, 0, flash_size) && _within(slots[1].addr, slots[1].len, 0, flash_size);
}

uint32_t ota_flash_capacity(uint32_t measured)
{
    return measured != 0 ? measured : OTA_FLASH_ASSUMED_SIZE;
}

int ota_boot_plan(int read_result, const ota_slot_t slots[2], uint32_t flash_size)
{
    switch (read_result) {
    case OTA_OK:
        return OTA_PLAN_CHECK;
    case OTA_ERR_LAYOUT:
        // 版面不同的映像收不到更新，不能確認。分割表是寫入這個映像的接收端用的那份，槽放得進 Flash 時
        // 照它記錄試開，試開逾時用完次數後退回。放不進時寫入會繞回 Flash 開頭：只讀不寫，讀錯也只是不擋解鎖
        return ota_slots_fit(slots, flash_size) ? OTA_PLAN_CHECK_NO_CONFIRM : OTA_PLAN_BLOCK_ONLY;
    default:
        return OTA_PLAN_SKIP;
    }
}

uint32_t ota_image_limit(const ota_slot_t slots[2])
{
    const uint32_t len = slots[0].len < slots[1].len ? slots[0].len : slots[1].len;
    return len > OTA_SECTOR_SIZE ? len - OTA_SECTOR_SIZE : 0;
}

int ota_label_valid(const ota_flash_t *f, const ota_slot_t *slot)
{
    uint8_t buf[OTA_LABEL_LEN];
    return f->read(f->ctx, slot->addr, buf, sizeof(buf)) == 0 && memcmp(buf, ota_label, OTA_LABEL_LEN) == 0;
}

int ota_parse_header(const uint8_t buf[OTA_HEADER_LEN], ota_header_t *h)
{
    const uint16_t version = (uint16_t)(buf[4] | (buf[5] << 8));
    if (_le32(buf) != OTA_MAGIC || version != OTA_VERSION) {
        return OTA_ERR_BAD_HEADER;
    }
    h->flags = (uint16_t)(buf[6] | (buf[7] << 8));
    h->image_len = _le32(buf + 8);
    h->checksum = _le32(buf + 12);
    return h->image_len < OTA_LABEL_LEN ? OTA_ERR_BAD_HEADER : OTA_OK;
}

int ota_auth_header(const uint8_t *public_key, uint32_t public_key_len, const uint8_t msg[OTA_AUTH_MSG_LEN],
                    const uint8_t sig[OTA_SIG_LEN], ota_signed_t *out)
{
    if (public_key == NULL || public_key_len != OTA_PUBLIC_KEY_LEN) {
        return OTA_ERR_NO_KEY;
    }
    if (crypto_check(sig, public_key, msg, OTA_AUTH_MSG_LEN) != 0) {
        return OTA_ERR_AUTH;
    }
    memcpy(out->header, msg, OTA_HEADER_LEN);
    memcpy(out->hash, msg + OTA_HEADER_LEN, OTA_HASH_LEN);
    return OTA_OK;
}

int ota_begin(ota_session_t *s, const ota_slot_t slots[2], int running_idx, uint32_t image_len, uint32_t checksum)
{
    memset(s, 0, sizeof(*s));
    if (running_idx != 1 && running_idx != 2) {
        return OTA_ERR_STATE;
    }
    if (!_slots_ok(slots)) {
        return OTA_ERR_PARTITION;
    }
    if (image_len < OTA_LABEL_LEN) {
        return OTA_ERR_BAD_HEADER;
    }
    if (image_len > ota_image_limit(slots)) {
        return OTA_ERR_TOO_LARGE;
    }
    s->running = slots[running_idx - 1];
    s->target = slots[2 - running_idx];
    s->image_len = image_len;
    s->checksum = checksum;
    s->active = 1;
    return OTA_OK;
}

static int _fail(ota_session_t *s, int status)
{
    ota_abort(s);
    return status;
}

void ota_abort(ota_session_t *s)
{
    crypto_wipe(&s->hash, sizeof(s->hash));
    s->active = 0;
    s->authed = 0;
    s->verified = 0;
}

int ota_auth_start(ota_session_t *s, const ota_signed_t *signed_msg)
{
    if (!s->active || s->authed) {
        return _fail(s, OTA_ERR_STATE);
    }
    memcpy(s->expected, signed_msg->hash, OTA_HASH_LEN);
    crypto_blake2b_init(&s->hash);
    s->authed = 1;
    return OTA_OK;
}

int ota_range_allowed(const ota_session_t *s, uint32_t addr, uint32_t len)
{
    return s->active && len > 0 && _within(addr, len, s->target.addr, s->target.len) &&
           !_overlaps(addr, len, s->running.addr, s->running.len);
}

// OTA 的所有抹除與寫入都經過這兩個函式：位址不在另一槽、或碰到目前的槽就拒絕
static int _erase(const ota_session_t *s, const ota_flash_t *f, uint32_t addr)
{
    if (addr % OTA_SECTOR_SIZE != 0 || !ota_range_allowed(s, addr, OTA_SECTOR_SIZE)) {
        return OTA_ERR_RANGE;
    }
    return f->erase_sector(f->ctx, addr) == 0 ? OTA_OK : OTA_ERR_FLASH;
}

static int _program(const ota_session_t *s, const ota_flash_t *f, uint32_t addr, const uint8_t *buf, uint32_t len)
{
    if (!ota_range_allowed(s, addr, len)) {
        return OTA_ERR_RANGE;
    }
    return f->program(f->ctx, addr, buf, len) == 0 ? OTA_OK : OTA_ERR_FLASH;
}

int ota_write(ota_session_t *s, const ota_flash_t *f, const uint8_t *data, uint32_t len)
{
    if (!s->active || !s->authed) {
        return OTA_ERR_STATE;
    }
    if (len > s->image_len - s->received) {
        return _fail(s, OTA_ERR_RANGE);
    }
    // 雜湊涵蓋收到的每個位元組，包括先留在記憶體的標籤
    crypto_blake2b_update(&s->hash, data, len);
    while (len > 0) {
        const uint32_t off = s->received;
        const uint32_t sector_left = OTA_SECTOR_SIZE - off % OTA_SECTOR_SIZE;
        const uint32_t n = len < sector_left ? len : sector_left;
        int r = OTA_OK;
        if (off % OTA_SECTOR_SIZE == 0) {
            r = _erase(s, f, s->target.addr + off);
        }
        // 前 8 bytes 是標籤：先留在記憶體，ota_finish 最後才寫
        uint32_t skip = 0;
        if (off < OTA_LABEL_LEN) {
            skip = OTA_LABEL_LEN - off < n ? OTA_LABEL_LEN - off : n;
            memcpy(s->label + off, data, skip);
        }
        if (r == OTA_OK && n > skip) {
            r = _program(s, f, s->target.addr + off + skip, data + skip, n - skip);
        }
        if (r != OTA_OK) {
            return _fail(s, r);
        }
        s->received += n;
        data += n;
        len -= n;
    }
    return OTA_OK;
}

int ota_verify(ota_session_t *s, const ota_flash_t *f)
{
    if (!s->active || !s->authed || s->verified || s->received != s->image_len) {
        return _fail(s, OTA_ERR_STATE);
    }
    s->authed = 0;
    uint8_t got[OTA_HASH_LEN];
    crypto_blake2b_final(&s->hash, got);
    if (crypto_verify64(got, s->expected) != 0) {
        return _fail(s, OTA_ERR_BAD_IMAGE);
    }
    if (memcmp(s->label, ota_label, OTA_LABEL_LEN) != 0) {
        return _fail(s, OTA_ERR_BAD_IMAGE);
    }
    // 讀回整個映像核對位元組總和；標籤用記憶體裡的副本
    uint32_t sum = 0;
    for (uint32_t i = 0; i < OTA_LABEL_LEN; i++) {
        sum += s->label[i];
    }
    uint8_t buf[256];
    for (uint32_t off = OTA_LABEL_LEN; off < s->image_len;) {
        const uint32_t n = s->image_len - off < sizeof(buf) ? s->image_len - off : (uint32_t)sizeof(buf);
        if (f->read(f->ctx, s->target.addr + off, buf, n) != 0) {
            return _fail(s, OTA_ERR_FLASH);
        }
        for (uint32_t i = 0; i < n; i++) {
            sum += buf[i];
        }
        off += n;
    }
    if (sum != s->checksum) {
        return _fail(s, OTA_ERR_CHECKSUM);
    }
    s->verified = 1;
    return OTA_OK;
}

int ota_activate(ota_session_t *s, const ota_flash_t *f)
{
    if (!s->active || !s->verified) {
        return _fail(s, OTA_ERR_STATE);
    }
    // 先寫試開紀錄，最後才寫標籤：標籤出現時紀錄一定已經在
    uint8_t rec[OTA_TRIAL_RECORD_LEN];
    memset(rec, 0xFF, sizeof(rec));
    _put_le32(rec, OTA_TRIAL_MAGIC);
    _put_le32(rec + 4, OTA_TRIAL_PENDING);
    int r = _erase(s, f, _trial_addr(&s->target));
    if (r == OTA_OK) {
        r = _program(s, f, _trial_addr(&s->target), rec, sizeof(rec));
    }
    if (r == OTA_OK) {
        r = _program(s, f, s->target.addr, s->label, OTA_LABEL_LEN);
    }
    if (r == OTA_OK && !ota_label_valid(f, &s->target)) {
        r = OTA_ERR_FLASH;
    }
    return _fail(s, r);
}

int ota_finish(ota_session_t *s, const ota_flash_t *f)
{
    const int r = ota_verify(s, f);
    return r == OTA_OK ? ota_activate(s, f) : r;
}

int ota_boot_check(const ota_flash_t *f, const ota_slot_t slots[2], int running_idx, uint32_t *attempts)
{
    *attempts = 0;
    if ((running_idx != 1 && running_idx != 2) || !_slots_ok(slots)) {
        return OTA_BOOT_NORMAL;
    }
    const ota_slot_t *run = &slots[running_idx - 1];
    const ota_slot_t *other = &slots[2 - running_idx];
    uint8_t rec[OTA_TRIAL_RECORD_LEN];
    if (f->read(f->ctx, _trial_addr(run), rec, sizeof(rec)) != 0) {
        return OTA_BOOT_ERROR;
    }
    // 不在試開期間（沒有紀錄或已確認）：不寫 Flash
    if (_le32(rec) != OTA_TRIAL_MAGIC || _le32(rec + 4) != OTA_TRIAL_PENDING) {
        return OTA_BOOT_NORMAL;
    }
    const uint32_t bitmap = _le32(rec + 8);
    uint32_t n = 0;
    while (n < 32 && (bitmap & (1u << n)) == 0) {
        n++;
    }
    *attempts = n;
    if (n >= OTA_TRIAL_MAX_ATTEMPTS) {
        if (!ota_label_valid(f, other)) {
            return OTA_BOOT_NO_FALLBACK;
        }
        // 設計上的例外：直接寫目前槽開頭的 8 bytes（自己的標籤），不經過 OTA 工作階段的範圍檢查。
        // 槽已由 _slots_ok 限制在 OTA 的區域內，而且只把位元清成 0
        static const uint8_t cleared[OTA_LABEL_LEN] = {0};
        return f->program(f->ctx, run->addr, cleared, OTA_LABEL_LEN) == 0 ? OTA_BOOT_ROLLBACK : OTA_BOOT_ERROR;
    }
    uint8_t word[4];
    _put_le32(word, bitmap & ~(1u << n));
    if (f->program(f->ctx, _trial_addr(run) + 8, word, sizeof(word)) != 0) {
        return OTA_BOOT_ERROR;
    }
    *attempts = n + 1;
    return OTA_BOOT_TRIAL;
}

int ota_confirm(const ota_flash_t *f, const ota_slot_t slots[2], int running_idx)
{
    if ((running_idx != 1 && running_idx != 2) || !_slots_ok(slots)) {
        return OTA_ERR_STATE;
    }
    const ota_slot_t *run = &slots[running_idx - 1];
    uint8_t rec[OTA_TRIAL_RECORD_LEN];
    if (f->read(f->ctx, _trial_addr(run), rec, sizeof(rec)) != 0) {
        return OTA_ERR_FLASH;
    }
    if (_le32(rec) != OTA_TRIAL_MAGIC || _le32(rec + 4) != OTA_TRIAL_PENDING) {
        return OTA_OK;
    }
    static const uint8_t confirmed[4] = {0, 0, 0, 0};
    return f->program(f->ctx, _trial_addr(run) + 4, confirmed, sizeof(confirmed)) == 0 ? OTA_OK : OTA_ERR_FLASH;
}

int ota_trial_pending(const ota_flash_t *f, const ota_slot_t slots[2], int running_idx)
{
    if ((running_idx != 1 && running_idx != 2) || !_slots_ok(slots)) {
        return 0;
    }
    uint8_t rec[OTA_TRIAL_RECORD_LEN];
    if (f->read(f->ctx, _trial_addr(&slots[running_idx - 1]), rec, sizeof(rec)) != 0) {
        return -1;
    }
    return _le32(rec) == OTA_TRIAL_MAGIC && _le32(rec + 4) == OTA_TRIAL_PENDING;
}

int ota_confirm_ready(uint32_t since_boot_ms, int receiver_ready, int main_loop_healthy, int wifi_associated)
{
    return since_boot_ms >= OTA_CONFIRM_AFTER_MS && receiver_ready && main_loop_healthy && wifi_associated;
}

int ota_trial_timeout_due(ota_trial_timer_t *t, uint32_t since_boot_ms, int confirmed, int armed)
{
    if (confirmed) {
        return 0;
    }
    // 飛行中重開會墜機：解鎖中一律不重開，並重設寬限的起點
    if (armed) {
        t->was_armed = 1;
        t->disarmed_at_valid = 0;
        return 0;
    }
    if (t->was_armed && !t->disarmed_at_valid) {
        t->disarmed_at_ms = since_boot_ms;
        t->disarmed_at_valid = 1;
    }
    if (since_boot_ms < OTA_TRIAL_TIMEOUT_MS) {
        return 0;
    }
    return !t->was_armed || since_boot_ms - t->disarmed_at_ms >= OTA_TRIAL_DISARM_GRACE_MS;
}
