/*
  AP_Filesystem 的 RTL8735B 後端：板上 microSD 卡的 FatFs。
  改寫自 AP_Filesystem_FATFS.cpp（ArduPilot 9f648ccabc），和它不同的地方：
  - 卡的開關與掛載由本檔的 sdcard_retry()／sdcard_stop() 經 AP_HAL_RTL8735B/sdcard_shim.h 做，
    取代 AP_HAL_ChibiOS/sdcard.cpp；本平台開機時沒有掛載的流程，第一次檔案操作時掛載，
    掛不上時每 3 秒最多再試一次；
  - 讀寫不必依 DMA 安全與否切段：sdcard_shim 自己處理未對齊的緩衝區；
  - 格式化前先確定卡已初始化：卡上沒有檔案系統時掛載失敗，卡也會被關掉；
  - get_fattime() 以 AP_RTC 的時間給 FatFs 時間戳；
  - 沒有 CrashDump；不另外定義 strerror（newlib 已有）；掛載成功不印訊息（LOG UART 可能跑 MAVLink）；
  - disk_free() 出錯時回傳 -1（上游回傳 FRESULT）。
 */
#include "AP_Filesystem_config.h"

#if AP_FILESYSTEM_RTL8735B_ENABLED

#include "AP_Filesystem.h"
#include "AP_Filesystem_RTL8735B.h"
#include <AP_HAL/AP_HAL.h>
#include <AP_Math/AP_Math.h>
#include <stdio.h>
#include <AP_Common/time.h>

#include <ff.h>
#include <AP_HAL_RTL8735B/sdcard_shim.h>
#include <GCS_MAVLink/GCS.h>
#include <AP_RTC/AP_RTC.h>

#if 0
#define debug(fmt, args ...)  do {printf("%s:%d: " fmt "\n", __FUNCTION__, __LINE__, ## args); } while(0)
#else
#define debug(fmt, args ...)
#endif

extern const AP_HAL::HAL& hal;

#define FATFS_R (S_IRUSR | S_IRGRP | S_IROTH)	/*< FatFs Read perms */
#define FATFS_W (S_IWUSR | S_IWGRP | S_IWOTH)	/*< FatFs Write perms */
#define FATFS_X (S_IXUSR | S_IXGRP | S_IXOTH)	/*< FatFs Execute perms */


// 每次 fsync 之前建議寫的量（bytes_until_fsync）
static const uint32_t io_size = AP_FATFS_MAX_IO_SIZE;


// use a semaphore to ensure that only one filesystem operation is
// happening at a time. A recursive semaphore is used to cope with the
// mkdir() inside sdcard_retry()
// 這把號誌同時保護 FatFs 與 sdcard_shim（兩者都不可重入）
static HAL_Semaphore sem;

typedef struct {
    FIL fobj; // should be first member; it's the most used
    char *name;
} FAT_FILE;

#define MAX_FILES 16
static FAT_FILE *file_table[MAX_FILES];

/*
  allocate a file descriptor
*/
static int new_file_descriptor(const char *pathname, FIL * &fh)
{
    int i;
    FAT_FILE *stream;

    for (i=0; i<MAX_FILES; ++i) {
        if (file_table[i] == NULL) {
            stream = (FAT_FILE *) calloc(1, sizeof(FAT_FILE));
            if (stream == NULL) {
                errno = ENOMEM;
                return -1;
            }
            stream->name = strdup(pathname);
            if (stream->name == NULL) {
                free(stream);
                errno = ENOMEM;
                return -1;
            }

            file_table[i]  = stream;
            fh = &stream->fobj;
            return i;
        }
    }
    errno = ENFILE;
    return -1;
}

static FAT_FILE *fileno_to_stream(int fileno)
{
    FAT_FILE *stream;
    if (fileno < 0 || fileno >= MAX_FILES) {
        errno = EBADF;
        return nullptr;
    }

    stream = file_table[fileno];
    if (stream == NULL) {
        errno = EBADF;
        return nullptr;
    }
    return stream;
}

static void free_file_descriptor(int fileno)
{
    FAT_FILE *stream = fileno_to_stream(fileno);
    if (stream != nullptr) {
        file_table[fileno] = NULL;
        free(stream->name);
        free(stream);
    }
}

static FIL *fileno_to_fatfs(int fileno)
{
    FAT_FILE *stream;

    stream = fileno_to_stream(fileno);
    if (stream == nullptr) { // unknown fileno?
        return nullptr; // errno already set
    }

    return &stream->fobj;
}

static int fatfs_to_errno(FRESULT Result)
{
    switch (Result) {
    case FR_OK:              /* FatFS (0) Succeeded */
        return 0;          /* POSIX OK */
    case FR_DISK_ERR:        /* FatFS (1) A hard error occurred in the low level disk I/O layer */
        return EIO;        /* POSIX Input/output error (POSIX.1) */

    case FR_INT_ERR:         /* FatFS (2) Assertion failed */
        return EPERM;      /* POSIX Operation not permitted (POSIX.1) */

    case FR_NOT_READY:       /* FatFS (3) The physical drive cannot work */
        return EBUSY;      /* POSIX Device or resource busy (POSIX.1) */

    case FR_NO_FILE:         /* FatFS (4) Could not find the file */
        return ENOENT;     /* POSIX No such file or directory (POSIX.1) */

    case FR_NO_PATH:         /* FatFS (5) Could not find the path */
        return ENOENT;     /* POSIX No such file or directory (POSIX.1) */

    case FR_INVALID_NAME:    /* FatFS (6) The path name format is invalid */
        return EINVAL;     /* POSIX Invalid argument (POSIX.1) */

    case FR_DENIED:          /* FatFS (7) Access denied due to prohibited access or directory full */
        return EACCES;     /* POSIX Permission denied (POSIX.1) */

    case FR_EXIST:           /* file exists */
        return EEXIST;     /* file exists */

    case FR_INVALID_OBJECT:  /* FatFS (9) The file/directory object is invalid */
        return EINVAL;     /* POSIX Invalid argument (POSIX.1) */

    case FR_WRITE_PROTECTED: /* FatFS (10) The physical drive is write protected */
        return EROFS;      /* POSIX Read-only filesystem (POSIX.1) */

    case FR_INVALID_DRIVE:   /* FatFS (11) The logical drive number is invalid */
        return ENXIO;      /* POSIX No such device or address (POSIX.1) */

    case FR_NOT_ENABLED:     /* FatFS (12) The volume has no work area */
        return ENOSPC;     /* POSIX No space left on device (POSIX.1) */

    case FR_NO_FILESYSTEM:   /* FatFS (13) There is no valid FAT volume */
        return ENXIO;      /* POSIX No such device or address (POSIX.1) */

    case FR_MKFS_ABORTED:    /* FatFS (14) The f_mkfs() aborted due to any parameter error */
        return EINVAL;     /* POSIX Invalid argument (POSIX.1) */

    case FR_TIMEOUT:         /* FatFS (15) Could not get a grant to access the volume within defined period */
        return EBUSY;      /* POSIX Device or resource busy (POSIX.1) */

    case FR_LOCKED:          /* FatFS (16) The operation is rejected according to the file sharing policy */
        return EBUSY;      /* POSIX Device or resource busy (POSIX.1) */


    case FR_NOT_ENOUGH_CORE: /* FatFS (17) LFN working buffer could not be allocated */
        return ENOMEM;     /* POSIX Not enough space (POSIX.1) */

    case FR_TOO_MANY_OPEN_FILES:/* FatFS (18) Number of open files > _FS_SHARE */
        return EMFILE;     /* POSIX Too many open files (POSIX.1) */

    case FR_INVALID_PARAMETER:/* FatFS (19) Given parameter is invalid */
        return EINVAL;     /* POSIX Invalid argument (POSIX.1) */

    }
    return EBADMSG;            /* POSIX Bad message (POSIX.1) */
}

// 開機時還沒掛載：設為 true，第一次檔案操作就經 remount_file_system() 掛載
static bool remount_needed = true;

static FATFS sd_fs;
static bool sdcard_running;
static bool last_mount_failed;
static uint32_t last_mount_fail_ms;

// 沒插卡時每次識別都要等驅動逾時：掛載失敗後至少隔這麼久才再試（同 AP_HAL_ChibiOS 與 AP_HAL_ESP32
// 在 IO 執行緒裡重試的間隔）。掛載成功後因讀寫錯誤重掛，不受這個限制
#define SDCARD_RETRY_MS 3000

/*
  stop sdcard interface (for reboot)
 */
static void sdcard_stop(void)
{
    f_mount(nullptr, "/", 1);
    rtl8735b_sdcard_deinit();
    sdcard_running = false;
}

/*
  還沒掛載時掛載。同 AP_HAL_ChibiOS/sdcard.cpp 的 sdcard_init()：試三次，成功後建 /APM
 */
static bool sdcard_retry(void)
{
    if (sdcard_running) {
        return true;
    }
    if (last_mount_failed && AP_HAL::millis() - last_mount_fail_ms < SDCARD_RETRY_MS) {
        return false;
    }
    for (uint8_t i = 0; i < 3; i++) {
        if (rtl8735b_sdcard_init() != RTL8735B_SDCARD_OK) {
            continue;
        }
        if (f_mount(&sd_fs, "/", 1) == FR_OK) {
            sdcard_running = true;
            break;
        }
        sdcard_stop();
    }
    last_mount_failed = !sdcard_running;
    if (!sdcard_running) {
        last_mount_fail_ms = AP_HAL::millis();
        return false;
    }
    debug("Successfully mounted SDCard");
#if AP_FILESYSTEM_FILE_WRITING_ENABLED
    // 上游經 AP::FS().mkdir()；這裡已持有號誌，直接呼叫 FatFs
    f_mkdir("/APM");
#endif
    return true;
}

// check for a remount and return -1 if it fails
#define CHECK_REMOUNT() do { if (remount_needed && !remount_file_system()) { errno = EIO; return -1; }} while (0)
#define CHECK_REMOUNT_NULL() do { if (remount_needed && !remount_file_system()) { errno = EIO; return NULL; }} while (0)

// we allow for IO retries if either not armed or not in main thread
#define RETRY_ALLOWED() (!hal.scheduler->in_main_thread() || !hal.util->get_soft_armed())

/*
  try to remount the file system on disk error
 */
static bool remount_file_system(void)
{
    EXPECT_DELAY_MS(3000);
    if (!remount_needed) {
        sdcard_stop();
    }
    if (!sdcard_retry()) {
        remount_needed = true;
        return false;
    }
    remount_needed = false;
    for (uint16_t i=0; i<MAX_FILES; i++) {
        FAT_FILE *f = file_table[i];
        if (!f) {
            continue;
        }
        FIL *fh = &f->fobj;
        FSIZE_t offset = fh->fptr;
        uint8_t flags = fh->flag & (FA_READ | FA_WRITE);

        memset(fh, 0, sizeof(*fh));
        if (flags & FA_WRITE) {
            // the file may not have been created yet on the sdcard
            flags |= FA_OPEN_ALWAYS;
        }
        FRESULT res = f_open(fh, f->name, flags);
        debug("reopen %s flags=0x%x ofs=%u -> %d\n", f->name, unsigned(flags), unsigned(offset), int(res));
        if (res == FR_OK) {
            f_lseek(fh, offset);
        }
    }
    return true;
}

int AP_Filesystem_RTL8735B::open(const char *pathname, int flags, bool allow_absolute_path)
{
    int fileno;
    int fatfs_modes;
    FIL *fh;
    int res;

    FS_CHECK_ALLOWED(-1);
    WITH_SEMAPHORE(sem);

    CHECK_REMOUNT();

    errno = 0;
    debug("Open %s 0x%x", pathname, flags);

    if ((flags & O_ACCMODE) == O_RDWR) {
        fatfs_modes = FA_READ | FA_WRITE;
    } else if ((flags & O_ACCMODE) == O_RDONLY) {
        fatfs_modes = FA_READ;
    } else {
        fatfs_modes = FA_WRITE;
    }

    if (flags & O_CREAT) {
        if (flags & O_TRUNC) {
            fatfs_modes |= FA_CREATE_ALWAYS;
        } else {
            fatfs_modes |= FA_OPEN_ALWAYS;
        }
    }

    fileno = new_file_descriptor(pathname, fh);
    if (fileno < 0) { // creation failed?
        return -1; // errno already set
    }

    res = f_open(fh, pathname, (BYTE) (fatfs_modes & 0xff));
    if (res == FR_DISK_ERR && RETRY_ALLOWED()) {
        // one retry on disk error
        hal.scheduler->delay(100);
        if (remount_file_system()) {
            res = f_open(fh, pathname, (BYTE) (fatfs_modes & 0xff));
        }
    }
    if (res != FR_OK) {
        errno = fatfs_to_errno((FRESULT)res);
        free_file_descriptor(fileno);
        return -1;
    }
    if (flags & O_APPEND) {
        ///  Seek to end of the file
        res = f_lseek(fh, f_size(fh));
        if (res != FR_OK) {
            errno = fatfs_to_errno((FRESULT)res);
            f_close(fh);
            free_file_descriptor(fileno);
            return -1;
        }
    }

    debug("Open %s -> %d", pathname, fileno);

    return fileno;
}

int AP_Filesystem_RTL8735B::close(int fileno)
{
    FIL *fh;
    int res;

    FS_CHECK_ALLOWED(-1);
    WITH_SEMAPHORE(sem);

    errno = 0;

    fh = fileno_to_fatfs(fileno);
    if (fh == nullptr) { // unknown fileno?
        return -1; // errno already set
    }
    res = f_close(fh);
    free_file_descriptor(fileno);
    if (res != FR_OK) {
        errno = fatfs_to_errno((FRESULT)res);
        return -1;
    }
    return 0;
}

int32_t AP_Filesystem_RTL8735B::read(int fd, void *buf, uint32_t count)
{
    UINT bytes = count;
    int res;
    FIL *fh;

    FS_CHECK_ALLOWED(-1);
    WITH_SEMAPHORE(sem);

    CHECK_REMOUNT();

    if (count > 0) {
        *(char *) buf = 0;
    }

    errno = 0;

    fh = fileno_to_fatfs(fd);
    if (fh == nullptr) { // unknown fd?
        return -1; // errno already set
    }

    UINT total = 0;
    do {
        UINT size = 0;
        UINT n = bytes;
        res = f_read(fh, (void *)buf, n, &size);
        if (res != FR_OK) {
            errno = fatfs_to_errno((FRESULT)res);
            return -1;
        }
        if (size == 0) {
            break;
        }
        if (size > n) {
            errno = EIO;
            return -1;
        }
        total += size;
        buf = (void *)(((uint8_t *)buf)+size);
        bytes -= size;
        if (size < n) {
            break;
        }
    } while (bytes > 0);
    return (ssize_t)total;
}

int32_t AP_Filesystem_RTL8735B::write(int fd, const void *buf, uint32_t count)
{
    UINT bytes = count;
    FRESULT res;
    FIL *fh;
    errno = 0;

    FS_CHECK_ALLOWED(-1);
    WITH_SEMAPHORE(sem);

    CHECK_REMOUNT();

    fh = fileno_to_fatfs(fd);
    if (fh == nullptr) { // unknown fd?
        return -1; // errno already set
    }

    UINT total = 0;
    do {
        UINT n = bytes;
        UINT size = 0;
        res = f_write(fh, buf, n, &size);
        if (res == FR_DISK_ERR && RETRY_ALLOWED()) {
            // one retry on disk error
            hal.scheduler->delay(100);
            if (remount_file_system()) {
                res = f_write(fh, buf, n, &size);
            }
        }
        if (size > n || size == 0) {
            errno = EIO;
            return -1;
        }
        if (res != FR_OK || size > n) {
            errno = fatfs_to_errno(res);
            return -1;
        }
        total += size;
        buf = (void *)(((uint8_t *)buf)+size);
        bytes -= size;
        if (size < n) {
            break;
        }
    } while (bytes > 0);
    return (ssize_t)total;
}

int AP_Filesystem_RTL8735B::fsync(int fileno)
{
    FIL *fh;
    int res;

    FS_CHECK_ALLOWED(-1);
    WITH_SEMAPHORE(sem);

    errno = 0;

    fh = fileno_to_fatfs(fileno);
    if (fh == nullptr) { // unknown fileno?
        return -1; // errno already set
    }
    res = f_sync(fh);
    if (res != FR_OK) {
        errno = fatfs_to_errno((FRESULT)res);
        return -1;
    }
    return 0;
}

int32_t AP_Filesystem_RTL8735B::lseek(int fileno, int32_t position, int whence)
{
    FRESULT res;
    FIL *fh;
    errno = 0;

    FS_CHECK_ALLOWED(-1);
    WITH_SEMAPHORE(sem);

    fh = fileno_to_fatfs(fileno);
    if (fh == nullptr) { // unknown fileno?
        return -1; // errno already set
    }

    if (whence == SEEK_END) {
        position += f_size(fh);
    } else if (whence==SEEK_CUR) {
        position += fh->fptr;
    }

    res = f_lseek(fh, position);
    if (res) {
        errno = fatfs_to_errno(res);
        return -1;
    }
    return fh->fptr;
}

static time_t fat_time_to_unix(uint16_t date, uint16_t time)
{
    struct tm tp;
    time_t unix;

    memset(&tp, 0, sizeof(struct tm));

    tp.tm_sec = (time << 1) & 0x3e;               // 2 second resolution
    tp.tm_min = ((time >> 5) & 0x3f);
    tp.tm_hour = ((time >> 11) & 0x1f);
    tp.tm_mday = (date & 0x1f);
    tp.tm_mon = ((date >> 5) & 0x0f) - 1;
    tp.tm_year = ((date >> 9) & 0x7f) + 80;
    unix = ap_mktime(&tp);
    return unix;
}

int AP_Filesystem_RTL8735B::stat(const char *name, struct stat *buf)
{
    FILINFO info;
    int res;
    time_t epoch;
    uint16_t mode;

    FS_CHECK_ALLOWED(-1);
    WITH_SEMAPHORE(sem);

    CHECK_REMOUNT();

    errno = 0;

    // f_stat does not handle / or . as root directory
    if (strcmp(name,"/") == 0 || strcmp(name,".") == 0) {
        buf->st_atime = 0;
        buf->st_mtime = 0;
        buf->st_ctime = 0;
        buf->st_uid= 0;
        buf->st_gid= 0;
        buf->st_size = 0;
        buf->st_mode = S_IFDIR;
        return 0;
    }

    res = f_stat(name, &info);
    if (res == FR_DISK_ERR && RETRY_ALLOWED()) {
        // one retry on disk error
        if (remount_file_system()) {
            res = f_stat(name, &info);
        }
    }
    if (res != FR_OK) {
        errno = fatfs_to_errno((FRESULT)res);
        return -1;
    }

    buf->st_size = info.fsize;
    epoch = fat_time_to_unix(info.fdate, info.ftime);
    buf->st_atime = epoch;                        // Access time
    buf->st_mtime = epoch;                        // Modification time
    buf->st_ctime = epoch;                        // Creation time

    // We only handle read only case
    mode = (FATFS_R | FATFS_X);
    if (!(info.fattrib & AM_RDO)) {
        mode |= (FATFS_W);    // enable write if NOT read only
    }

    if (info.fattrib & AM_SYS) {
        buf->st_uid= 0;
        buf->st_gid= 0;
    }
    {
        buf->st_uid=1000;
        buf->st_gid=1000;
    }

    if (info.fattrib & AM_DIR) {
        mode |= S_IFDIR;
    } else {
        mode |= S_IFREG;
    }
    buf->st_mode = mode;

    return 0;
}

int AP_Filesystem_RTL8735B::unlink(const char *pathname)
{
    FS_CHECK_ALLOWED(-1);
    WITH_SEMAPHORE(sem);

    errno = 0;
    int res = f_unlink(pathname);
    if (res != FR_OK) {
        errno = fatfs_to_errno((FRESULT)res);
        return -1;
    }
    return 0;
}

int AP_Filesystem_RTL8735B::mkdir(const char *pathname)
{
    FS_CHECK_ALLOWED(-1);
    WITH_SEMAPHORE(sem);

    errno = 0;

    int res = f_mkdir(pathname);
    if (res != FR_OK) {
        errno = fatfs_to_errno((FRESULT)res);
        return -1;
    }

    return 0;
}

int AP_Filesystem_RTL8735B::rename(const char *oldpath, const char *newpath)
{
    FS_CHECK_ALLOWED(-1);
    WITH_SEMAPHORE(sem);

    errno = 0;

    int res = f_rename(oldpath, newpath);
    if (res != FR_OK) {
        errno = fatfs_to_errno((FRESULT)res);
        return -1;
    }

    return 0;
}

/*
  wrapper structure to associate a dirent with a DIR
 */
struct DIR_Wrapper {
    DIR d; // must be first structure element
    struct dirent de;
};

void *AP_Filesystem_RTL8735B::opendir(const char *pathdir)
{
    FS_CHECK_ALLOWED(nullptr);
    WITH_SEMAPHORE(sem);

    CHECK_REMOUNT_NULL();

    debug("Opendir %s", pathdir);
    struct DIR_Wrapper *ret = NEW_NOTHROW DIR_Wrapper;
    if (!ret) {
        return nullptr;
    }
    int res = f_opendir(&ret->d, pathdir);
    if (res == FR_DISK_ERR && RETRY_ALLOWED()) {
        // one retry on disk error
        if (remount_file_system()) {
            res = f_opendir(&ret->d, pathdir);
        }
    }
    if (res != FR_OK) {
        errno = fatfs_to_errno((FRESULT)res);
        delete ret;
        return nullptr;
    }
    debug("Opendir %s -> %p", pathdir, ret);
    return &ret->d;
}

struct dirent *AP_Filesystem_RTL8735B::readdir(void *dirp_void)
{
    FS_CHECK_ALLOWED(nullptr);
    WITH_SEMAPHORE(sem);
    DIR *dirp = (DIR *)dirp_void;

    struct DIR_Wrapper *d = (struct DIR_Wrapper *)dirp;
    if (!d) {
        errno = EINVAL;
        return nullptr;
    }
    FILINFO fno;
    int len;
    int res;

    d->de.d_name[0] = 0;
    res = f_readdir(dirp, &fno);
    if (res != FR_OK || fno.fname[0] == 0) {
        errno = fatfs_to_errno((FRESULT)res);
        return nullptr;
    }
    len = MIN(strlen(fno.fname),sizeof(d->de.d_name)-1);
    strncpy_noterm(d->de.d_name,fno.fname,len);
    d->de.d_name[len] = 0;
    if (fno.fattrib & AM_DIR) {
        d->de.d_type = DT_DIR;
    } else {
        d->de.d_type = DT_REG;
    }
    return &d->de;
}

int AP_Filesystem_RTL8735B::closedir(void *dirp_void)
{
    DIR *dirp = (DIR *)dirp_void;
    FS_CHECK_ALLOWED(-1);
    WITH_SEMAPHORE(sem);

    struct DIR_Wrapper *d = (struct DIR_Wrapper *)dirp;
    if (!d) {
        errno = EINVAL;
        return -1;
    }
    int res = f_closedir (dirp);
    delete d;
    if (res != FR_OK) {
        errno = fatfs_to_errno((FRESULT)res);
        return -1;
    }
    debug("closedir");
    return 0;
}

// return number of bytes that should be written before fsync for optimal
// streaming performance/robustness. if zero, any number can be written.
// assume similar to old logging code that max-IO-size boundaries are good.
uint32_t AP_Filesystem_RTL8735B::bytes_until_fsync(int fd)
{
    FS_CHECK_ALLOWED(0);
    WITH_SEMAPHORE(sem);

    FIL *fh = fileno_to_fatfs(fd);
    if (fh == nullptr) { // unknown fd?
        return 0; // return "any number", the write/fsync will fail anyway
    }

    const uint32_t block_size = io_size;

    uint32_t block_pos = fh->fptr % block_size;
    return block_size - block_pos;
}

// return free disk space in bytes
int64_t AP_Filesystem_RTL8735B::disk_free(const char *path)
{
    FS_CHECK_ALLOWED(-1);
    WITH_SEMAPHORE(sem);

    FATFS *fs;
    DWORD fre_clust, fre_sect;

    CHECK_REMOUNT();

    /* Get volume information and free clusters of drive 1 */
    FRESULT res = f_getfree("/", &fre_clust, &fs);
    if (res) {
        return -1;
    }

    /* Get total sectors and free sectors */
    fre_sect = fre_clust * fs->csize;
    return (int64_t)(fre_sect)*512;
}

// return total disk space in bytes
int64_t AP_Filesystem_RTL8735B::disk_space(const char *path)
{
    FS_CHECK_ALLOWED(-1);
    WITH_SEMAPHORE(sem);

    CHECK_REMOUNT();

    FATFS *fs;
    DWORD fre_clust, tot_sect;

    /* Get volume information and free clusters of drive 1 */
    FRESULT res = f_getfree("/", &fre_clust, &fs);
    if (res) {
        return -1;
    }

    /* Get total sectors and free sectors */
    tot_sect = (fs->n_fatent - 2) * fs->csize;
    return (int64_t)(tot_sect)*512;
}

/*
  convert unix time_t to FATFS timestamp
 */
static void unix_time_to_fat(time_t epoch, uint16_t &date, uint16_t &time)
{
    struct tm tmd {};
    struct tm *t = gmtime_r((time_t *)&epoch, &tmd);

    /* Pack date and time into a uint32_t variable */
    date = ((uint16_t)(t->tm_year - 80) << 9)
        | (((uint16_t)t->tm_mon+1) << 5)
        | (((uint16_t)t->tm_mday));

    time = ((uint16_t)t->tm_hour << 11)
        | ((uint16_t)t->tm_min << 5)
        | ((uint16_t)t->tm_sec >> 1);
}

/*
  set mtime on a file
 */
bool AP_Filesystem_RTL8735B::set_mtime(const char *filename, const uint32_t mtime_sec)
{
    FILINFO fno;
    uint16_t fdate, ftime;

    unix_time_to_fat(mtime_sec, fdate, ftime);

    fno.fdate = fdate;
    fno.ftime = ftime;

    FS_CHECK_ALLOWED(false);
    WITH_SEMAPHORE(sem);

    return f_utime(filename, (FILINFO *)&fno) == FR_OK;
}

/*
  retry mount of filesystem if needed
*/
bool AP_Filesystem_RTL8735B::retry_mount(void)
{
    FS_CHECK_ALLOWED(false);
    WITH_SEMAPHORE(sem);
    return sdcard_retry();
}

/*
  unmount filesystem for reboot
*/
void AP_Filesystem_RTL8735B::unmount(void)
{
    WITH_SEMAPHORE(sem);
    return sdcard_stop();
}

/*
  format sdcard
*/
bool AP_Filesystem_RTL8735B::format(void)
{
#if FF_USE_MKFS
    WITH_SEMAPHORE(sem);
    hal.scheduler->register_io_process(FUNCTOR_BIND_MEMBER(&AP_Filesystem_RTL8735B::format_handler, void));
    // the format is handled asynchronously, we inform user of success
    // via a text message.  format_status can be polled for progress
    format_status = FormatStatus::PENDING;
    return true;
#else
    return false;
#endif
}

/*
  format sdcard
*/
void AP_Filesystem_RTL8735B::format_handler(void)
{
#if FF_USE_MKFS
    if (format_status != FormatStatus::PENDING) {
        return;
    }
    WITH_SEMAPHORE(sem);
    format_status = FormatStatus::IN_PROGRESS;
    GCS_SEND_TEXT(MAV_SEVERITY_NOTICE, "Formatting SDCard");
    uint8_t *buf = (uint8_t *)hal.util->malloc_type(FF_MAX_SS, AP_HAL::Util::MEM_DMA_SAFE);
    if (buf == nullptr) {
        return;
    }
    // 卡上沒有檔案系統時掛載失敗、卡也被關掉；格式化前先確定卡已初始化
    if (!rtl8735b_sdcard_ready() && rtl8735b_sdcard_init() != RTL8735B_SDCARD_OK) {
        hal.util->free_type(buf, FF_MAX_SS, AP_HAL::Util::MEM_DMA_SAFE);
        format_status = FormatStatus::FAILURE;
        GCS_SEND_TEXT(MAV_SEVERITY_NOTICE, "Format: Failed (no card)");
        return;
    }
    // format first disk
    auto ret = f_mkfs("0:", 0, buf, FF_MAX_SS);
    hal.util->free_type(buf, FF_MAX_SS, AP_HAL::Util::MEM_DMA_SAFE);
    if (ret == FR_OK) {
        format_status = FormatStatus::SUCCESS;
        GCS_SEND_TEXT(MAV_SEVERITY_NOTICE, "Format: OK");
    } else {
        format_status = FormatStatus::FAILURE;
        GCS_SEND_TEXT(MAV_SEVERITY_NOTICE, "Format: Failed (%d)", int(ret));
    }
    sdcard_stop();
    // 格式化前的掛載失敗不算數，馬上重掛
    last_mount_failed = false;
    sdcard_retry();
#endif
}

// returns true if we are currently formatting the SD card:
AP_Filesystem_Backend::FormatStatus AP_Filesystem_RTL8735B::get_format_status(void) const
{
    // note that format_handler holds sem, so we can't take it here.
    return format_status;
}

/*
  FatFs 寫入檔案時取時間戳（FF_FS_NORTC 為 0 時由 FatFs 呼叫）。同 AP_HAL_ChibiOS 的
  hwdef/common/stm32_util.c：時間還沒設定時回傳固定的 1980-01-01
 */
extern "C" DWORD get_fattime(void)
{
    uint64_t utc_usec;
    if (!AP::rtc().get_utc_usec(utc_usec)) {
        return ((DWORD)1 << 16) | ((DWORD)1 << 21);
    }
    const time_t utc_sec = time_t(utc_usec / 1000000U);
    struct tm tmd {};
    const struct tm *t = gmtime_r(&utc_sec, &tmd);
    if (t == nullptr || t->tm_year < 80) {
        return ((DWORD)1 << 16) | ((DWORD)1 << 21);
    }
    return ((DWORD)(t->tm_year - 80) << 25) |
           ((DWORD)(t->tm_mon + 1) << 21) |
           ((DWORD)t->tm_mday << 16) |
           ((DWORD)t->tm_hour << 11) |
           ((DWORD)t->tm_min << 5) |
           ((DWORD)t->tm_sec >> 1);
}

#endif  // AP_FILESYSTEM_RTL8735B_ENABLED
