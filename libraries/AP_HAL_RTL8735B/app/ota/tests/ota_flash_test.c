/*
 * ota_flash.c 的主機測試：16 MiB 的假 NOR Flash（抹除設為 0xFF、寫入只能把位元由 1 清成 0），加上 XIP 的位址對映：
 * running_fw2 時，auto mode 的 flash_stream_read 讀 fw1 範圍會讀到 fw2 的內容；flash_burst_read 讀實體位址。
 * 寫入與抹除都落在實體位址。用法：ota_flash_test <案例>，通過時印 ok 並以 0 結束；不帶參數時列出案例。
 */
#include "ota_flash.h"

#include <flash_api.h>
#include <device_lock.h>
#include <hal_cache.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLASH_SIZE 0x1000000u
#define FW1_ADDR 0x60000u
#define FW2_ADDR 0x520000u
#define SLOT_LEN 0x400000u

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __func__, __LINE__, #c); exit(1); } } while (0)

static uint8_t *flash;
static int running_fw2;
static int locks, unlocks, stream_reads;
static uint8_t jedec[3] = {0xC8, 0x40, 0x18};
static int read_id_result = 3;

static uint32_t xip(uint32_t a)
{
    return running_fw2 && a >= FW1_ADDR && a < FW1_ADDR + SLOT_LEN ? a - FW1_ADDR + FW2_ADDR : a;
}

void flash_erase_sector(flash_t *obj, uint32_t address)
{
    (void)obj;
    memset(flash + (address & ~0xFFFu), 0xFF, 0x1000);
}

int flash_read_id(flash_t *obj, uint8_t *buf, uint8_t len)
{
    (void)obj;
    memcpy(buf, jedec, len < 3 ? len : 3);
    return read_id_result;
}

int flash_stream_read(flash_t *obj, uint32_t address, uint32_t len, uint8_t *data)
{
    (void)obj;
    stream_reads++;
    for (uint32_t i = 0; i < len; i++) {
        data[i] = flash[xip(address + i)];
    }
    return 1;
}

int flash_burst_read(flash_t *obj, uint32_t address, uint32_t Length, uint8_t *data)
{
    (void)obj;
    memcpy(data, flash + address, Length);
    return 1;
}

int flash_stream_write(flash_t *obj, uint32_t address, uint32_t len, uint8_t *data)
{
    (void)obj;
    for (uint32_t i = 0; i < len; i++) {
        flash[address + i] &= data[i];
    }
    return 1;
}

void device_mutex_lock(int dev)
{
    (void)dev;
    locks++;
}

void device_mutex_unlock(int dev)
{
    (void)dev;
    unlocks++;
}

void dcache_clean_invalidate_by_addr(void *addr, int32_t size)
{
    (void)addr;
    (void)size;
}

static void setup(void)
{
    flash = malloc(FLASH_SIZE);
    memset(flash, 0xFF, FLASH_SIZE);
    memset(flash + FW1_ADDR, 0x11, SLOT_LEN);
    memset(flash + FW2_ADDR, 0x22, SLOT_LEN);
    running_fw2 = 0;
    locks = unlocks = stream_reads = 0;
    rtl8735b_ota_flash_use_lock(1);
}

// 從 fw2 執行時讀 fw1：要讀到 fw1 的實體內容，不是 fw2
static void read_ignores_xip_remap(void)
{
    const ota_flash_t *f = rtl8735b_ota_flash();
    uint8_t buf[64];
    running_fw2 = 1;
    CHECK(f->read(f->ctx, FW1_ADDR + 0x1000, buf, sizeof(buf)) == 0);
    for (size_t i = 0; i < sizeof(buf); i++) { CHECK(buf[i] == 0x11); }
    CHECK(stream_reads == 0);
}

// 從 fw2 執行時把新映像寫進 fw1：抹除、寫入與讀回核對都要成功，資料落在 fw1、fw2 不受影響
static void update_fw1_while_running_fw2(void)
{
    const ota_flash_t *f = rtl8735b_ota_flash();
    uint8_t data[600];
    for (size_t i = 0; i < sizeof(data); i++) { data[i] = (uint8_t)(i * 7 + 3); }
    running_fw2 = 1;
    CHECK(f->erase_sector(f->ctx, FW1_ADDR) == 0);
    CHECK(f->program(f->ctx, FW1_ADDR + 0x80, data, sizeof(data)) == 0);
    CHECK(memcmp(flash + FW1_ADDR + 0x80, data, sizeof(data)) == 0);
    for (uint32_t i = 0; i < SLOT_LEN; i += 0x1000) { CHECK(flash[FW2_ADDR + i] == 0x22); }
}

// 寫入只能把位元清成 0：目的位置沒先抹除時讀回不符，回報失敗
static void program_without_erase_fails(void)
{
    const ota_flash_t *f = rtl8735b_ota_flash();
    const uint8_t data[4] = {0x5A, 0xA5, 0xFF, 0x00};
    CHECK(f->program(f->ctx, FW1_ADDR, data, sizeof(data)) != 0);
}

// 每次讀寫都上鎖、解鎖；開機檢查（use_lock 0）不上鎖
static void lock_only_in_task_mode(void)
{
    const ota_flash_t *f = rtl8735b_ota_flash();
    uint8_t buf[8];
    CHECK(f->read(f->ctx, FW1_ADDR, buf, sizeof(buf)) == 0);
    CHECK(locks > 0 && locks == unlocks);
    rtl8735b_ota_flash_use_lock(0);
    locks = unlocks = 0;
    CHECK(f->read(f->ctx, FW1_ADDR, buf, sizeof(buf)) == 0);
    CHECK(f->erase_sector(f->ctx, FW2_ADDR) == 0);
    CHECK(locks == 0 && unlocks == 0);
}

// 容量取自 JEDEC ID 第三個 byte；讀不到或超出 1 MiB–2 GiB 時回傳 0
static void flash_size_from_jedec(void)
{
    CHECK(rtl8735b_ota_flash_size() == 0x1000000u);
    jedec[2] = 0x17;
    CHECK(rtl8735b_ota_flash_size() == 0x800000u);
    jedec[2] = 0x13;
    CHECK(rtl8735b_ota_flash_size() == 0);
    jedec[2] = 0x20;
    CHECK(rtl8735b_ota_flash_size() == 0);
    jedec[2] = 0x18;
    read_id_result = -1;
    CHECK(rtl8735b_ota_flash_size() == 0);
}

static const struct { const char *name; void (*fn)(void); } cases[] = {
    {"read_ignores_xip_remap", read_ignores_xip_remap},
    {"update_fw1_while_running_fw2", update_fw1_while_running_fw2},
    {"program_without_erase_fails", program_without_erase_fails},
    {"lock_only_in_task_mode", lock_only_in_task_mode},
    {"flash_size_from_jedec", flash_size_from_jedec},
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
