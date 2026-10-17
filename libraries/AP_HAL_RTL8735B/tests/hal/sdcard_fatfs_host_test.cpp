/* SPDX-License-Identifier: GPL-3.0-or-later */
// SD 卡從檔案系統後端到區塊存取的主機測試：正式的 AP_Filesystem_RTL8735B.cpp、AP_Filesystem_backend.cpp、
// FatFs、sdcard_diskio.c、sdcard_shim.c 一起編譯，SDK 的 SD 主控函式表換成 RAM 上的假卡。
// AP_HAL、GCS、RTC 是替身（fatfs_fakes/）：時間、執行緒與解鎖狀態都由測試控制。
#include <AP_Filesystem/AP_Filesystem.h>
#include <AP_Filesystem/AP_Filesystem_RTL8735B.h>
#include <AP_Common/time.h>
#include <AP_HAL/AP_HAL.h>
#include <AP_RTC/AP_RTC.h>
#include <hal_cache.h>
#include <hal_sdhost.h>
#include <task.h>

#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr uint32_t BLK = 512;

// RAM 上的假卡：16 MiB
struct FakeCard {
    bool present = true;
    uint32_t capacity = 32768;
    std::vector<uint8_t> data = std::vector<uint8_t>(size_t(32768) * BLK, 0);
    int fail_writes = 0;          // 接下來幾次寫入回傳錯誤
    uint32_t init_card_calls = 0;
    uint32_t writes = 0;
} card;

uint32_t now_ms = 1000;
bool armed = false;
bool main_thread = false;
bool rtc_valid = false;
uint64_t rtc_usec = 0;
std::vector<AP_HAL::Proc> io_procs;

hal_status_t fake_init_host(hal_sdhost_adapter_t *a)
{
    memset(a, 0, sizeof(*a));
    return HAL_OK;
}

hal_status_t fake_init_card(hal_sdhost_adapter_t *a)
{
    card.init_card_calls++;
    if (!card.present) {
        return HAL_TIMEOUT;
    }
    a->blk_capacity = card.capacity;
    return HAL_OK;
}

void fake_deinit(hal_sdhost_adapter_t *)
{
}

hal_status_t fake_read(hal_sdhost_adapter_t *, u32 blk, u16 cnt, u8 *buf)
{
    assert(((uintptr_t)buf & 31u) == 0);
    assert(cnt > 0 && blk + cnt <= card.capacity);
    if (!card.present) {
        return HAL_ERR_HW;
    }
    memcpy(buf, &card.data[size_t(blk) * BLK], size_t(cnt) * BLK);
    return HAL_OK;
}

hal_status_t fake_write(hal_sdhost_adapter_t *, u32 blk, u16 cnt, const u8 *buf)
{
    assert(((uintptr_t)buf & 31u) == 0);
    assert(cnt > 0 && blk + cnt <= card.capacity);
    if (!card.present) {
        return HAL_ERR_HW;
    }
    if (card.fail_writes > 0) {
        card.fail_writes--;
        return HAL_ERR_HW;
    }
    card.writes++;
    memcpy(&card.data[size_t(blk) * BLK], buf, size_t(cnt) * BLK);
    return HAL_OK;
}

void fake_yield_hook(hal_sdhost_adapter_t *a, sdhost_para_cb_t cb, void *para)
{
    a->task_yield_cb = cb;
    a->task_yield_cb_para = para;
}

AP_HAL::Scheduler scheduler;
AP_HAL::Util util;
AP_HAL::HAL hal_instance{&scheduler, &util};
AP_RTC rtc_instance;

void run_io_procs()
{
    auto procs = io_procs;
    io_procs.clear();
    for (auto &p : procs) {
        p();
    }
}

std::set<std::string> list_dir(AP_Filesystem_RTL8735B &fs, const char *path, std::set<std::string> *dirs = nullptr)
{
    std::set<std::string> names;
    void *d = fs.opendir(path);
    assert(d != nullptr);
    while (struct dirent *de = fs.readdir(d)) {
        names.insert(de->d_name);
        if (dirs != nullptr && de->d_type == DT_DIR) {
            dirs->insert(de->d_name);
        }
    }
    assert(fs.closedir(d) == 0);
    return names;
}

std::vector<uint8_t> pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; i++) {
        v[i] = uint8_t(seed + i * 7u + (i >> 9));
    }
    return v;
}

// 沒插卡：每次掛載試三次；失敗後 3 秒內不再試，之後再試
void test_no_card(AP_Filesystem_RTL8735B &fs)
{
    card.present = false;
    errno = 0;
    assert(fs.open("/a.txt", O_RDONLY) == -1);
    assert(errno == EIO);
    assert(card.init_card_calls == 3);
    assert(fs.open("/a.txt", O_RDONLY) == -1);
    assert(fs.disk_space("/") == -1);
    assert(card.init_card_calls == 3);
    now_ms += 3001;
    assert(fs.open("/a.txt", O_RDONLY) == -1);
    assert(card.init_card_calls == 6);
}

// 插了沒格式化的卡：掛不上；format() 在 IO 程序裡格式化後掛上，並建好 /APM
void test_format(AP_Filesystem_RTL8735B &fs)
{
    card.present = true;
    now_ms += 3001;
    assert(fs.open("/a.txt", O_RDONLY) == -1);
    assert(fs.get_format_status() == AP_Filesystem_Backend::FormatStatus::NOT_STARTED);
    assert(fs.format());
    assert(fs.get_format_status() == AP_Filesystem_Backend::FormatStatus::PENDING);
    run_io_procs();
    assert(fs.get_format_status() == AP_Filesystem_Backend::FormatStatus::SUCCESS);
    struct stat st;
    assert(fs.stat("/APM", &st) == 0);
    assert(S_ISDIR(st.st_mode));
    assert(fs.disk_space("/") > 15 * 1024 * 1024);
}

void test_file_ops(AP_Filesystem_RTL8735B &fs)
{
    const int64_t free_before = fs.disk_free("/");
    assert(free_before > 0 && free_before <= fs.disk_space("/"));
    assert(fs.mkdir("/APM/LOGS") == 0);
    errno = 0;
    assert(fs.mkdir("/APM/LOGS") == -1 && errno == EEXIST);

    // 寫一個跨好幾個磁區、緩衝區沒有對齊的檔
    const std::vector<uint8_t> data = pattern(70001, 3);
    std::vector<uint8_t> raw(data.size() + 1);
    memcpy(&raw[1], data.data(), data.size());
    int fd = fs.open("/APM/LOGS/00000001.BIN", O_WRONLY | O_CREAT | O_TRUNC);
    assert(fd >= 0);
    assert(fs.write(fd, &raw[1], 30000) == 30000);
    assert(fs.write(fd, &raw[30001], data.size() - 30000) == int32_t(data.size() - 30000));
    assert(fs.fsync(fd) == 0);
    assert(fs.close(fd) == 0);
    struct stat st;
    assert(fs.stat("/APM/LOGS/00000001.BIN", &st) == 0);
    assert(st.st_size == off_t(data.size()) && S_ISREG(st.st_mode));
    assert(fs.disk_free("/") < free_before);

    // 讀回來到沒有對齊的緩衝區
    std::vector<uint8_t> back(data.size() + 3, 0);
    fd = fs.open("/APM/LOGS/00000001.BIN", O_RDONLY);
    assert(fd >= 0);
    assert(fs.read(fd, &back[3], uint32_t(data.size()) + 100) == int32_t(data.size()));
    assert(memcmp(&back[3], data.data(), data.size()) == 0);
    // lseek：絕對、相對、從結尾
    assert(fs.lseek(fd, 1000, SEEK_SET) == 1000);
    uint8_t b[4];
    assert(fs.read(fd, b, 4) == 4 && memcmp(b, &data[1000], 4) == 0);
    assert(fs.lseek(fd, 10, SEEK_CUR) == 1014);
    assert(fs.lseek(fd, -1, SEEK_END) == int32_t(data.size() - 1));
    assert(fs.read(fd, b, 4) == 1 && b[0] == data.back());
    assert(fs.close(fd) == 0);
    errno = 0;
    assert(fs.close(fd) == -1 && errno == EBADF);

    // O_APPEND 接在結尾
    fd = fs.open("/APM/LOGS/00000001.BIN", O_WRONLY | O_APPEND);
    assert(fd >= 0);
    assert(fs.write(fd, "end", 3) == 3);
    assert(fs.close(fd) == 0);
    assert(fs.stat("/APM/LOGS/00000001.BIN", &st) == 0 && st.st_size == off_t(data.size() + 3));

    // 目錄列表與 d_type
    assert(fs.rename("/APM/LOGS/00000001.BIN", "/APM/LOGS/LASTLOG.BIN") == 0);
    std::set<std::string> dirs;
    const auto names = list_dir(fs, "/APM", &dirs);
    assert(names.count("LOGS") == 1 && dirs.count("LOGS") == 1);
    const auto logs = list_dir(fs, "/APM/LOGS");
    assert(logs.size() == 1 && logs.count("LASTLOG.BIN") == 1);

    // 不存在的檔與刪除
    errno = 0;
    assert(fs.open("/nope.txt", O_RDONLY) == -1 && errno == ENOENT);
    assert(fs.unlink("/APM/LOGS/LASTLOG.BIN") == 0);
    assert(fs.stat("/APM/LOGS/LASTLOG.BIN", &st) == -1 && errno == ENOENT);
    assert(fs.stat("/", &st) == 0 && S_ISDIR(st.st_mode));

    // 長檔名
    const char *lfn = "/APM/a-rather-long-file-name-for-fat.param";
    fd = fs.open(lfn, O_WRONLY | O_CREAT | O_TRUNC);
    assert(fd >= 0 && fs.write(fd, "x", 1) == 1 && fs.close(fd) == 0);
    assert(list_dir(fs, "/APM").count("a-rather-long-file-name-for-fat.param") == 1);
}

// 時間戳：RTC 有時間時，FatFs 寫進的修改時間就是它（2 秒解析度）；set_mtime 也寫得進去
void test_mtime(AP_Filesystem_RTL8735B &fs)
{
    struct tm t {};
    t.tm_year = 2026 - 1900;
    t.tm_mon = 9;
    t.tm_mday = 18;
    t.tm_hour = 12;
    t.tm_min = 34;
    t.tm_sec = 56;
    const time_t when = ap_mktime(&t);
    rtc_valid = true;
    rtc_usec = uint64_t(when) * 1000000U + 123456U;
    int fd = fs.open("/APM/T.TXT", O_WRONLY | O_CREAT | O_TRUNC);
    assert(fd >= 0 && fs.write(fd, "t", 1) == 1 && fs.close(fd) == 0);
    struct stat st;
    assert(fs.stat("/APM/T.TXT", &st) == 0);
    assert(st.st_mtime == when);
    assert(fs.set_mtime("/APM/T.TXT", uint32_t(when - 3600 * 24)));
    assert(fs.stat("/APM/T.TXT", &st) == 0 && st.st_mtime == when - 3600 * 24);
    rtc_valid = false;
}

// 寫入時卡出一次錯：關掉卡、重掛、重開已開的檔，再寫一次就成功；fsync 過的內容還在
void test_write_error_remount(AP_Filesystem_RTL8735B &fs)
{
    const std::vector<uint8_t> a = pattern(4096, 11), b = pattern(4096, 99);
    int fd = fs.open("/APM/R.BIN", O_WRONLY | O_CREAT | O_TRUNC);
    assert(fd >= 0);
    assert(fs.write(fd, a.data(), 4096) == 4096);
    assert(fs.fsync(fd) == 0);
    const uint32_t inits = card.init_card_calls;
    card.fail_writes = 1;
    assert(fs.write(fd, b.data(), 4096) == 4096);
    assert(card.init_card_calls == inits + 1);
    assert(fs.close(fd) == 0);
    std::vector<uint8_t> back(8192);
    fd = fs.open("/APM/R.BIN", O_RDONLY);
    assert(fd >= 0 && fs.read(fd, back.data(), 8192) == 8192 && fs.close(fd) == 0);
    assert(memcmp(back.data(), a.data(), 4096) == 0 && memcmp(&back[4096], b.data(), 4096) == 0);
}

// 拔卡：寫入失敗；之後 3 秒內的操作不碰卡；插回去、過了 3 秒就重掛，已開的檔重新開好
void test_card_removed(AP_Filesystem_RTL8735B &fs)
{
    int fd = fs.open("/APM/P.BIN", O_WRONLY | O_CREAT | O_TRUNC);
    assert(fd >= 0 && fs.write(fd, "abc", 3) == 3 && fs.fsync(fd) == 0);
    card.present = false;
    std::vector<uint8_t> big = pattern(8192, 5);
    errno = 0;
    assert(fs.write(fd, big.data(), 8192) == -1);
    const uint32_t inits = card.init_card_calls;
    struct stat st;
    assert(fs.stat("/APM", &st) == -1 && errno == EIO);
    assert(card.init_card_calls == inits);
    card.present = true;
    assert(fs.stat("/APM", &st) == -1);
    now_ms += 3001;
    assert(fs.stat("/APM", &st) == 0);
    // 重開的檔從失敗那次寫入停下的位置接著寫（同上游；沒寫進卡的那段內容不保證）
    assert(fs.write(fd, "def", 3) == 3);
    assert(fs.close(fd) == 0);
    assert(fs.stat("/APM/P.BIN", &st) == 0 && st.st_size >= 6);
    std::vector<uint8_t> back(size_t(st.st_size));
    fd = fs.open("/APM/P.BIN", O_RDONLY);
    assert(fd >= 0 && fs.read(fd, back.data(), uint32_t(back.size())) == int32_t(back.size()));
    assert(fs.close(fd) == 0);
    assert(memcmp(back.data(), "abc", 3) == 0 && memcmp(&back[back.size() - 3], "def", 3) == 0);
}

// 主執行緒在解鎖狀態下不准做檔案操作，不碰卡
void test_armed_main_thread(AP_Filesystem_RTL8735B &fs)
{
    armed = true;
    main_thread = true;
    const uint32_t writes = card.writes;
    assert(fs.open("/APM/X.BIN", O_WRONLY | O_CREAT) == -1);
    assert(fs.mkdir("/APM/X") == -1);
    assert(card.writes == writes);
    armed = false;
    main_thread = false;
}

// 卸載（重開機前）之後，retry_mount() 重新掛上
void test_unmount(AP_Filesystem_RTL8735B &fs)
{
    fs.unmount();
    assert(fs.retry_mount());
    struct stat st;
    assert(fs.stat("/APM/T.TXT", &st) == 0 && st.st_size == 1);
}

}  // namespace

const AP_HAL::HAL &hal = hal_instance;

uint32_t AP_HAL::millis()
{
    return now_ms;
}

void AP_HAL::Scheduler::delay(uint16_t ms)
{
    now_ms += ms;
}

bool AP_HAL::Scheduler::in_main_thread() const
{
    return main_thread;
}

void AP_HAL::Scheduler::register_io_process(AP_HAL::Proc proc)
{
    io_procs.push_back(proc);
}

bool AP_HAL::Util::get_soft_armed() const
{
    return armed;
}

void *AP_HAL::Util::malloc_type(size_t size, Memory_Type)
{
    return calloc(1, size);
}

void AP_HAL::Util::free_type(void *ptr, size_t, Memory_Type)
{
    free(ptr);
}

bool AP_RTC::get_utc_usec(uint64_t &usec) const
{
    usec = rtc_usec;
    return rtc_valid;
}

AP_RTC &AP::rtc()
{
    return rtc_instance;
}

time_t ap_mktime(const struct tm *t)
{
    struct tm copy = *t;
    return timegm(&copy);
}

size_t strncpy_noterm(char *dest, const char *src, size_t n)
{
    size_t len = strnlen(src, n);
    if (len < n) {
        len++;
    }
    memcpy(dest, src, len);
    return len;
}

extern "C" {
const hal_sdhost_func_stubs_t hal_sdhost_stubs = {
    fake_init_host,
    fake_init_card,
    fake_deinit,
    fake_read,
    fake_write,
    fake_yield_hook,
};

void dcache_invalidate_by_addr(uint32_t *, int32_t)
{
}

void dcache_clean_by_addr(uint32_t *, int32_t)
{
}

void vTaskDelay(TickType_t ticks)
{
    now_ms += ticks;
}
}

int main()
{
    // 正式程式裡後端是靜態物件（AP_Filesystem.cpp 的 fs_local），成員清零
    static AP_Filesystem_RTL8735B fs;
    test_no_card(fs);
    test_format(fs);
    test_file_ops(fs);
    test_mtime(fs);
    test_write_error_remount(fs);
    test_card_removed(fs);
    test_armed_main_thread(fs);
    test_unmount(fs);
    printf("sdcard fatfs: all tests passed\n");
    return 0;
}
