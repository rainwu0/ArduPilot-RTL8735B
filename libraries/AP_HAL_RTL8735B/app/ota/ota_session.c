/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * 一條 OTA 連線的協定，見 ota_session.h。
 */

#include "ota_session.h"

#include <string.h>

static void _put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static int _send_status(const ota_io_t *io, uint32_t status)
{
    uint8_t b[OTA_STATUS_LEN];
    _put_le32(b, status);
    return io->send(io->ctx, b, sizeof(b)) == 0 ? OTA_OK : OTA_ERR_IO;
}

// 收滿 len bytes。until 是絕對期限；idle_us 不為 0 時另外檢查閒置
static int _recv_exact(const ota_io_t *io, uint8_t *buf, uint32_t len, uint64_t until, uint64_t idle_us)
{
    uint64_t last = io->now_us(io->ctx);
    uint32_t got = 0;
    while (got < len) {
        const int n = io->recv(io->ctx, buf + got, len - got);
        const uint64_t now = io->now_us(io->ctx);
        if (n < 0 || (uint32_t)n > len - got) {
            return OTA_ERR_IO;
        }
        if (n > 0) {
            got += (uint32_t)n;
            last = now;
            continue;
        }
        if (now >= until || (idle_us > 0 && now - last >= idle_us)) {
            return OTA_ERR_TIMEOUT;
        }
        io->wait(io->ctx);
    }
    return OTA_OK;
}

static int _receive_image(ota_session_ctx_t *c, uint64_t until, uint64_t idle_us)
{
    const ota_io_t *io = c->io;
    uint64_t last = io->now_us(io->ctx);
    uint32_t step = 0;
    while (c->s.received < c->s.image_len) {
        const uint32_t left = c->s.image_len - c->s.received;
        const uint32_t want = left < sizeof(c->buf) ? left : (uint32_t)sizeof(c->buf);
        const int n = io->recv(io->ctx, c->buf, want);
        const uint64_t now = io->now_us(io->ctx);
        if (n < 0 || (uint32_t)n > want) {
            return OTA_ERR_IO;
        }
        // 總上限在一直有資料時也要成立
        if (now >= until) {
            return OTA_ERR_TIMEOUT;
        }
        if (n == 0) {
            if (now - last >= idle_us) {
                return OTA_ERR_TIMEOUT;
            }
            io->wait(io->ctx);
            continue;
        }
        last = now;
        if (io->armed(io->ctx)) {
            return OTA_ERR_ARMED;
        }
        const int r = ota_write(&c->s, c->flash, c->buf, (uint32_t)n);
        if (r != OTA_OK) {
            return r;
        }
        // 診斷：每跨過一個 5% 叫一次（image_len 至少是標籤長度，不會是 0）
        if (io->progress != NULL) {
            const uint32_t now_step =
                (uint32_t)(((uint64_t)c->s.received * OTA_PROGRESS_STEPS) / c->s.image_len);
            if (now_step > step) {
                step = now_step;
                io->progress(io->ctx, c->s.received, c->s.image_len);
            }
        }
    }
    return OTA_OK;
}

int ota_is_probe(const uint8_t buf[OTA_HEADER_LEN])
{
    const uint32_t magic = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
    return magic == OTA_PROBE_MAGIC;
}

// 唯讀的試開探測：讀分割表與目前槽的試開紀錄，回狀態與目前的槽，不寫 Flash、不設旗標
static int _serve_probe(ota_session_ctx_t *c)
{
    const ota_io_t *io = c->io;
    ota_slot_t slots[2];
    uint8_t reply[OTA_PROBE_REPLY_LEN];
    int r = ota_read_slots(c->flash, c->layout, slots);
    if (r == OTA_OK) {
        const int pending = ota_trial_pending(c->flash, slots, c->running_idx);
        r = pending < 0 ? OTA_ERR_FLASH : pending ? OTA_ERR_BUSY : OTA_OK;
    }
    _put_le32(reply, (uint32_t)r);
    _put_le32(reply + 4, (uint32_t)c->running_idx);
    if (io->send(io->ctx, reply, sizeof(reply)) != 0) {
        return OTA_ERR_IO;
    }
    return r;
}

int ota_session_serve(ota_session_ctx_t *c)
{
    const ota_io_t *io = c->io;
    uint8_t req[OTA_REQUEST_LEN];
    ota_signed_t signed_msg;
    ota_header_t h;
    ota_slot_t slots[2];
    int r;

    c->reboot = 0;
    c->in_progress = 0;
    c->probe = 0;
    const uint64_t t0 = io->now_us(io->ctx);
    const uint64_t auth_until = t0 + OTA_T_AUTH_MS * 1000ull;
    const uint64_t total_until = t0 + OTA_T_TOTAL_MS * 1000ull;
    const uint64_t idle_us = OTA_T_IDLE_MS * 1000ull;

    // 已解鎖：不送任何東西
    if (io->armed(io->ctx)) {
        return OTA_ERR_ARMED;
    }
    if (c->public_key == NULL || c->public_key_len != OTA_PUBLIC_KEY_LEN) {
        (void)_send_status(io, OTA_ERR_NO_KEY);
        return OTA_ERR_NO_KEY;
    }
    if (_send_status(io, OTA_OK) != OTA_OK) {
        return OTA_ERR_IO;
    }

    // 簽章驗過之前不設旗標、不碰 Flash；先驗簽章、再解析標頭
    r = _recv_exact(io, req, OTA_HEADER_LEN, auth_until, 0);
    if (r == OTA_OK && ota_is_probe(req)) {
        c->probe = 1;
        return _serve_probe(c);
    }
    if (r == OTA_OK) {
        r = _recv_exact(io, req + OTA_HEADER_LEN, sizeof(req) - OTA_HEADER_LEN, auth_until, 0);
    }
    if (r == OTA_OK) {
        r = ota_auth_header(c->public_key, c->public_key_len, req, req + OTA_AUTH_MSG_LEN, &signed_msg);
    }
    if (r == OTA_OK) {
        r = ota_parse_header(signed_msg.header, &h);
    }
    if (r != OTA_OK) {
        goto reply;
    }

    c->in_progress = 1;
    if (io->armed(io->ctx)) {
        r = OTA_ERR_ARMED;
    }
    if (r == OTA_OK) {
        r = ota_read_slots(c->flash, c->layout, slots);
    }
    if (r == OTA_OK) {
        // 目前的映像還在試開：另一槽是唯一確認過的映像，不能蓋掉
        const int pending = ota_trial_pending(c->flash, slots, c->running_idx);
        r = pending < 0 ? OTA_ERR_FLASH : pending ? OTA_ERR_BUSY : OTA_OK;
    }
    if (r == OTA_OK) {
        r = ota_begin(&c->s, slots, c->running_idx, h.image_len, h.checksum);
    }
    if (r == OTA_OK) {
        r = ota_auth_start(&c->s, &signed_msg);
    }
    if (r != OTA_OK) {
        goto reply;
    }
    if (_send_status(io, OTA_OK) != OTA_OK) {
        r = OTA_ERR_IO;
        goto out;
    }

    r = _receive_image(c, total_until, idle_us);
    if (r == OTA_OK && io->armed(io->ctx)) {
        r = OTA_ERR_ARMED;
    }
    if (r == OTA_OK) {
        r = ota_verify(&c->s, c->flash);
    }
    // 讀回核對整個映像要一段時間；寫試開紀錄與標籤之前再檢查一次
    if (r == OTA_OK && io->armed(io->ctx)) {
        r = OTA_ERR_ARMED;
    }
    if (r == OTA_OK) {
        r = ota_activate(&c->s, c->flash);
    }
    if (r == OTA_OK) {
        // 映像已啟用；最後的狀態送不出去也照常決定是否重開
        (void)_send_status(io, OTA_OK);
        c->reboot = !(h.flags & OTA_FLAG_NO_REBOOT) && !io->armed(io->ctx);
        goto out;
    }

reply:
    if (r != OTA_ERR_IO) {
        (void)_send_status(io, (uint32_t)r);
    }
out:
    ota_abort(&c->s);
    // 要重開時旗標保持設定到重置為止（呼叫端不重置時自行清除）；其他情況一律清除
    if (!c->reboot) {
        c->in_progress = 0;
    }
    return r;
}
