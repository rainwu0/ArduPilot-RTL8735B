/*
 * ota_session.c 接上本機 TCP（POSIX socket）與假 NOR Flash，給電腦端 rtl8735b_ota_send.py 做端對端測試。
 *   ota_session_tcp <公鑰 hex 或 -> <firmware_ntz.bin 或 -> [armed|trial]
 * 在 127.0.0.1 的任意埠 listen，印出「port N」後服務一條連線；結束時印
 * 「result <狀態> reboot <0|1> match <0|1> running_same <0|1> erase <次數> program <次數>」：
 *   match：fw2 槽的內容等於映像、標籤已寫、試開紀錄是待確認；
 *   running_same：分割表與目前的槽（fw1）一個位元組都沒變。
 * armed：已解鎖；trial：目前的槽（fw1）還在試開。
 */
#define _POSIX_C_SOURCE 200809L

#include "ota_session.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define FLASH_SIZE 0x1000000u
#define FW1_ADDR 0x80000u
#define FW2_ADDR 0x4C0000u
#define SLOT_LEN 0x380000u
#define PART_ADDR 0x2000u
#define PART_LEN 0x100u

static uint8_t *flash;
static int armed;
static unsigned erase_count;
static unsigned program_count;

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int fk_read(void *c, uint32_t a, uint8_t *b, uint32_t n)
{
    (void)c;
    if ((uint64_t)a + n > FLASH_SIZE) { return -1; }
    memcpy(b, flash + a, n);
    return 0;
}

static int fk_erase(void *c, uint32_t a)
{
    (void)c;
    if (a % OTA_SECTOR_SIZE != 0 || (uint64_t)a + OTA_SECTOR_SIZE > FLASH_SIZE) { return -1; }
    erase_count++;
    memset(flash + a, 0xFF, OTA_SECTOR_SIZE);
    return 0;
}

static int fk_program(void *c, uint32_t a, const uint8_t *b, uint32_t n)
{
    (void)c;
    if ((uint64_t)a + n > FLASH_SIZE) { return -1; }
    program_count++;
    for (uint32_t i = 0; i < n; i++) { flash[a + i] &= b[i]; }
    return 0;
}

static int io_recv(void *c, uint8_t *buf, uint32_t len)
{
    const ssize_t n = recv(*(int *)c, buf, len, MSG_DONTWAIT);
    if (n == 0) { return -1; }
    if (n < 0) { return errno == EAGAIN || errno == EWOULDBLOCK ? 0 : -1; }
    return (int)n;
}

static int io_send(void *c, const uint8_t *buf, uint32_t len)
{
    while (len > 0) {
        const ssize_t n = send(*(int *)c, buf, len, 0);
        if (n <= 0) { return -1; }
        buf += n;
        len -= (uint32_t)n;
    }
    return 0;
}

static uint64_t io_now(void *c)
{
    (void)c;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static void io_wait(void *c)
{
    (void)c;
    const struct timespec ts = {0, 1000000};
    nanosleep(&ts, NULL);
}

static int io_armed(void *c)
{
    (void)c;
    return armed;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: ota_session_tcp <pubkey-hex|-> <firmware_ntz.bin|-> [armed|trial]\n");
        return 2;
    }
    uint8_t key[OTA_PUBLIC_KEY_LEN];
    uint32_t key_len = 0;
    if (strcmp(argv[1], "-") != 0) {
        if (strlen(argv[1]) != 2 * OTA_PUBLIC_KEY_LEN) { return 2; }
        for (; key_len < OTA_PUBLIC_KEY_LEN; key_len++) {
            unsigned v;
            if (sscanf(argv[1] + 2 * key_len, "%2x", &v) != 1) { return 2; }
            key[key_len] = (uint8_t)v;
        }
    }
    const char *mode = argc > 3 ? argv[3] : "";
    armed = strcmp(mode, "armed") == 0;

    static uint8_t image[SLOT_LEN];
    size_t image_len = 0;
    if (strcmp(argv[2], "-") != 0) {
        FILE *f = fopen(argv[2], "rb");
        if (f == NULL) { return 2; }
        image_len = fread(image, 1, sizeof(image), f);
        fclose(f);
    }

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
    if (strcmp(mode, "trial") == 0) {
        put_le32(flash + FW1_ADDR + SLOT_LEN - OTA_SECTOR_SIZE, OTA_TRIAL_MAGIC);
        put_le32(flash + FW1_ADDR + SLOT_LEN - OTA_SECTOR_SIZE + 4, OTA_TRIAL_PENDING);
    }
    uint8_t *snap = malloc(PART_LEN + SLOT_LEN);
    memcpy(snap, flash + PART_ADDR, PART_LEN);
    memcpy(snap + PART_LEN, flash + FW1_ADDR, SLOT_LEN);

    const int lfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t addr_len = sizeof(addr);
    if (lfd < 0 || bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(lfd, 1) != 0 ||
        getsockname(lfd, (struct sockaddr *)&addr, &addr_len) != 0) {
        return 2;
    }
    printf("port %u\n", (unsigned)ntohs(addr.sin_port));
    fflush(stdout);
    int fd = accept(lfd, NULL, NULL);
    if (fd < 0) { return 2; }

    const ota_flash_t fl = {fk_read, fk_erase, fk_program, NULL};
    const ota_io_t io = {io_recv, io_send, io_now, io_wait, io_armed, &fd, NULL};
    static ota_session_ctx_t ctx;
    ctx.io = &io;
    ctx.flash = &fl;
    ctx.public_key = key;
    ctx.public_key_len = key_len;
    ctx.running_idx = 1;
    static const ota_layout_t layout = {{{FW1_ADDR, SLOT_LEN}, {FW2_ADDR, SLOT_LEN}}, FLASH_SIZE};
    ctx.layout = &layout;
    const int r = ota_session_serve(&ctx);
    close(fd);
    close(lfd);
    const int match = image_len > OTA_LABEL_LEN && memcmp(flash + FW2_ADDR, image, image_len) == 0 &&
                      memcmp(flash + FW2_ADDR, ota_label, OTA_LABEL_LEN) == 0 &&
                      le32(flash + FW2_ADDR + SLOT_LEN - OTA_SECTOR_SIZE) == OTA_TRIAL_MAGIC &&
                      le32(flash + FW2_ADDR + SLOT_LEN - OTA_SECTOR_SIZE + 4) == OTA_TRIAL_PENDING;
    const int running_same = memcmp(snap, flash + PART_ADDR, PART_LEN) == 0 &&
                             memcmp(snap + PART_LEN, flash + FW1_ADDR, SLOT_LEN) == 0;
    printf("result %d reboot %d match %d running_same %d erase %u program %u\n", r, ctx.reboot, match, running_same,
           erase_count, program_count);
    return 0;
}
