#!/usr/bin/env bash
# HAL 的主機測試（Wi-Fi 以外）：序列埠、SPI/I²C、IO 與看門狗、RC 輸入與參數儲存、排程、SD 卡。
# 正式的 HAL 原始碼配替身 SDK、FreeRTOS 與 AP_HAL 在主機上編譯執行；不開序列埠、不碰開發板。
# HAL 原始碼預設是本目錄上兩層的 libraries/AP_HAL_RTL8735B，可用 $1 指定；只跑一組時給第二個參數。
set -euo pipefail
ulimit -c 0
lib_dir=$(cd "${1:-$(dirname "${BASH_SOURCE[0]}")/../..}" && pwd)
only=${2:-}
test_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
out=$(mktemp -d "${TMPDIR:-/tmp}/rtl8735b-hal-test.XXXXXX")
trap 'rm -rf -- "$out"' EXIT
CFLAGS=(-std=c11 -Wall -Wextra -Werror)
CXXFLAGS=(-std=c++17 -Wall -Wextra -Werror)

run_uart() {
    c++ "${CXXFLAGS[@]}" -pthread -I"$test_dir/fakes" -I"$lib_dir/.." -I"$lib_dir" \
        "$lib_dir/UARTDriver.cpp" "$test_dir/uart_driver_host_test.cpp" -o "$out/uart-driver-test"
    timeout 60 "$out/uart-driver-test"

    # sdk_shim.c 的 UART 中斷順序：shim 引用的 SDK 標頭在暫存目錄產生轉接檔，一律導向替身 SDK。
    # __wrap_fputc/__wrap_fputs 參照的 newlib __real_* 在主機上不存在，靠 --gc-sections 移除。
    local uart_sdk="$out/uart_sdk" name
    mkdir -p "$uart_sdk"
    for name in hal_timer.h sys_api.h hal_uart.h objects.h serial_api.h serial_ex_api.h cmsis_compiler.h; do
        printf '#include "fake_uart_sdk.h"\n' > "$uart_sdk/$name"
    done
    cc "${CFLAGS[@]}" -ffunction-sections -I"$uart_sdk" -I"$test_dir/fakes/uart_sdk" -I"$lib_dir" \
        "$lib_dir/sdk_shim.c" "$test_dir/uart_shim_host_test.c" \
        -Wl,--gc-sections -o "$out/uart-shim-test"
    timeout 30 "$out/uart-shim-test"

    python3 "$lib_dir/hwdef/scripts/rtl8735b_hwdef.py" --quiet --outdir "$out" \
        "$lib_dir/hwdef/rtl8735b-empty/hwdef.dat"
    cc "${CFLAGS[@]}" -I"$test_dir/fakes" -I"$out" -I"$lib_dir" \
        -c "$lib_dir/UARTConfig.c" -o "$out/uart-config.o"
    c++ "${CXXFLAGS[@]}" -I"$lib_dir" "$test_dir/uart_config_host_test.cpp" \
        "$out/uart-config.o" -o "$out/uart-config-test"
    timeout 30 "$out/uart-config-test"
}

run_bus() {
    cc "${CFLAGS[@]}" -I"$test_dir/fakes/bus_sdk" -I"$lib_dir" \
        -c "$lib_dir/bus_shim.c" -o "$out/bus-shim.o"
    c++ "${CXXFLAGS[@]}" -I"$test_dir/fakes/bus_sdk" -I"$lib_dir" \
        "$test_dir/bus_shim_host_test.cpp" "$out/bus-shim.o" -o "$out/bus-shim-test"
    timeout 30 "$out/bus-shim-test"
    timeout 30 "$out/bus-shim-test" reinit-failure
    timeout 30 "$out/bus-shim-test" alt-index
    timeout 30 "$out/bus-shim-test" init-clear
    timeout 30 "$out/bus-shim-test" pin-zero
    timeout 30 "$out/bus-shim-test" two-buses

    c++ "${CXXFLAGS[@]}" -pthread -I"$test_dir/fakes/device_bus" -I"$lib_dir" \
        "$lib_dir/DeviceBus.cpp" "$test_dir/bus_device_host_test.cpp" -o "$out/bus-device-test"
    timeout 30 "$out/bus-device-test"
    local kind
    for kind in I2C SPI; do
        c++ "${CXXFLAGS[@]}" -pthread -I"$test_dir/fakes/device_bus" -I"$lib_dir" \
            "$lib_dir/DeviceBus.cpp" "$lib_dir/${kind}Device.cpp" \
            "$test_dir/${kind,,}_device_host_test.cpp" -o "$out/${kind,,}-device-test"
        timeout 30 "$out/${kind,,}-device-test"
    done
    # 非預設的 I2C 表：邏輯匯流排 3、SDK 的 I2C2、內部匯流排
    c++ "${CXXFLAGS[@]}" -pthread -DTEST_I2C_BUS=3U -DTEST_I2C_INDEX=2U -DTEST_I2C_INTERNAL=1U \
        -I"$test_dir/fakes/device_bus" -I"$lib_dir" \
        "$lib_dir/DeviceBus.cpp" "$lib_dir/I2CDevice.cpp" \
        "$test_dir/i2c_device_host_test.cpp" -o "$out/i2c-device-alt-test"
    timeout 30 "$out/i2c-device-alt-test"
    # 兩條 I2C 匯流排：I2C1 內部、I2C2 外部，各自的 bus task 與遮罩
    c++ "${CXXFLAGS[@]}" -pthread -DTEST_TWO_I2C_BUSES=1 \
        -I"$test_dir/fakes/device_bus" -I"$lib_dir" \
        "$lib_dir/DeviceBus.cpp" "$lib_dir/I2CDevice.cpp" \
        "$test_dir/i2c_device_host_test.cpp" -o "$out/i2c-device-two-test"
    timeout 30 "$out/i2c-device-two-test"
}

run_io() {
    cc "${CFLAGS[@]}" -I"$test_dir/io_fakes" -I"$lib_dir" \
        -c "$lib_dir/IOConfig.c" -o "$out/io-config.o"
    c++ "${CXXFLAGS[@]}" -pthread -I"$test_dir/io_fakes" -I"$lib_dir" \
        "$lib_dir/RCOutput.cpp" "$lib_dir/StatusText.cpp" "$test_dir/io_contract_host_test.cpp" "$out/io-config.o" \
        -o "$out/io-contract-test"
    timeout 30 "$out/io-contract-test"

    cc "${CFLAGS[@]}" -I"$test_dir/io_fakes" -I"$lib_dir" \
        -c "$lib_dir/io_shim.c" -o "$out/io-shim.o"
    c++ "${CXXFLAGS[@]}" -pthread -I"$test_dir/io_fakes" -I"$lib_dir" \
        "$lib_dir/GPIO.cpp" "$lib_dir/AnalogIn.cpp" "$test_dir/io_sdk_fake.cpp" \
        "$test_dir/io_shim_host_test.cpp" "$out/io-config.o" "$out/io-shim.o" \
        -o "$out/io-shim-contract-test"
    timeout 30 "$out/io-shim-contract-test"
    timeout 30 "$out/io-shim-contract-test" oneshot125

    # 非預設的表（io_fakes/hwdef.h 的 IO_ALT_BOARD）：GPIO 編號、類比通道與 PWM 數量都跟著表走
    cc "${CFLAGS[@]}" -DIO_ALT_BOARD -I"$test_dir/io_fakes" -I"$lib_dir" \
        -c "$lib_dir/IOConfig.c" -o "$out/io-config-alt.o"
    cc "${CFLAGS[@]}" -DIO_ALT_BOARD -I"$test_dir/io_fakes" -I"$lib_dir" \
        -c "$lib_dir/io_shim.c" -o "$out/io-shim-alt.o"
    c++ "${CXXFLAGS[@]}" -DIO_ALT_BOARD -pthread -I"$test_dir/io_fakes" -I"$lib_dir" \
        "$lib_dir/GPIO.cpp" "$lib_dir/AnalogIn.cpp" "$test_dir/io_sdk_fake.cpp" \
        "$test_dir/io_shim_host_test.cpp" "$out/io-config-alt.o" "$out/io-shim-alt.o" \
        -o "$out/io-shim-alt-board-test"
    timeout 30 "$out/io-shim-alt-board-test"

    # 硬體看門狗：Watchdog（啟動條件、餵狗、預期延遲）以替身 wdt_shim 測；wdt_shim.c 以替身 SDK 標頭測
    c++ "${CXXFLAGS[@]}" -I"$lib_dir" "$lib_dir/Watchdog.cpp" "$test_dir/watchdog_host_test.cpp" \
        -o "$out/watchdog-test"
    timeout 30 "$out/watchdog-test"
    cc "${CFLAGS[@]}" -I"$test_dir/wdt_fakes" -I"$lib_dir" \
        "$lib_dir/wdt_shim.c" "$test_dir/wdt_shim_host_test.c" -o "$out/wdt-shim-test"
    local scenario
    for scenario in start watchdog-reset other-reset; do
        timeout 30 "$out/wdt-shim-test" "$scenario"
    done
}

run_rc_storage() {
    c++ "${CXXFLAGS[@]}" -fno-rtti -pthread \
        -I"$test_dir/rc_storage_fakes" -I"$test_dir/fakes" -I"$lib_dir" -isystem "$lib_dir/.." \
        "$lib_dir/RCInput.cpp" "$test_dir/rc_storage_host_test.cpp" -o "$out/rc-test"
    timeout 30 "$out/rc-test"

    # Storage：正式的 Storage.cpp、storage_shim.c 與共用 AP_FlashStorage.cpp，SDK Flash 以假 NOR 取代。
    # -Wcast-align=strict：ARM 建置以 -Wcast-align -Werror 編譯，x86 上要 strict 才會同樣報錯
    cc "${CFLAGS[@]}" -O1 -Wcast-align=strict -I"$test_dir/storage_fakes/sdk" -I"$lib_dir" \
        -c "$lib_dir/storage_shim.c" -o "$out/storage-shim.o"
    c++ "${CXXFLAGS[@]}" -O1 -fno-rtti -pthread \
        -I"$test_dir/storage_fakes" -I"$test_dir/storage_fakes/sdk" -I"$test_dir/fakes" -I"$lib_dir" \
        -isystem "$lib_dir/.." \
        "$lib_dir/Storage.cpp" "$lib_dir/../AP_FlashStorage/AP_FlashStorage.cpp" \
        "$test_dir/storage_flash_fake.cpp" "$test_dir/storage_flash_host_test.cpp" "$out/storage-shim.o" \
        -o "$out/storage-test"
    timeout 120 "$out/storage-test"
}

run_scheduler() {
    # 替身目錄放最前面；AP_Math/div1000.h 等沒有替身的標頭取自正式的 libraries/。
    # Scheduler.cpp 有依介面保留但不使用的參數，所以關掉 -Wunused-parameter。
    c++ "${CXXFLAGS[@]}" -Wno-unused-parameter \
        -I"$test_dir/scheduler_fakes" -I"$lib_dir" -I"$lib_dir/.." \
        "$lib_dir/Scheduler.cpp" "$lib_dir/system.cpp" "$lib_dir/Watchdog.cpp" \
        "$test_dir/scheduler_host_test.cpp" -o "$out/scheduler-test"
    timeout 60 "$out/scheduler-test"

    # 韌體入口 main.c 的 rtl8735b_app_init：沒有開發板的應用程式時是空的弱符號；有應用程式
    # （CMake 定義 RTL8735B_APP_REQUIRED）時不提供，應用程式漏寫入口就在連結時報錯，不會悄悄變成空函式。
    local main_c="$lib_dir/targets/cmake/main.c" sym
    cc "${CFLAGS[@]}" -I"$test_dir/main_fakes" -c "$main_c" -o "$out/main-noapp.o"
    sym=$(nm "$out/main-noapp.o" | awk '$NF == "rtl8735b_app_init" {print $(NF-1)}')
    [ "$sym" = W ] || { echo "沒有應用程式時 rtl8735b_app_init 應是弱符號（W），實際是 '$sym'"; return 1; }
    cc "${CFLAGS[@]}" -DRTL8735B_APP_REQUIRED=1 -I"$test_dir/main_fakes" -c "$main_c" -o "$out/main-app.o"
    sym=$(nm "$out/main-app.o" | awk '$NF == "rtl8735b_app_init" {print $(NF-1)}')
    [ "$sym" = U ] || { echo "有應用程式時 rtl8735b_app_init 要由應用程式提供（U），實際是 '$sym'"; return 1; }
    grep -q 'RTL8735B_APP_REQUIRED' "$lib_dir/targets/cmake/CMakeLists.txt" ||
        { echo "CMakeLists.txt 在有應用程式時沒有定義 RTL8735B_APP_REQUIRED"; return 1; }
}

run_sdcard() {
    # SD 卡區塊存取：正式的 sdcard_shim.c，SDK 的 SD 主控函式表換成 RAM 上的假卡。
    # -Wcast-align=strict：理由同 Storage
    cc "${CFLAGS[@]}" -O1 -Wcast-align=strict -I"$test_dir/sdcard_fakes" -I"$lib_dir" \
        -c "$lib_dir/sdcard_shim.c" -o "$out/sdcard-shim.o"
    c++ "${CXXFLAGS[@]}" -I"$test_dir/sdcard_fakes" -I"$lib_dir" \
        "$test_dir/sdcard_shim_host_test.cpp" "$out/sdcard-shim.o" -o "$out/sdcard-shim-test"
    timeout 60 "$out/sdcard-shim-test"

    # 檔案系統後端、HAL 的 fatfs/（FatFs 與本平台的 ffconf.h）、磁碟介面與上面的 sdcard_shim 一起測。
    # FatFs 是第三方程式，不加 -Werror。
    local fatfs="$lib_dir/fatfs" libs="$lib_dir/.." f
    for f in ff ffunicode; do
        cc -std=gnu11 -O1 -I"$fatfs" -c "$fatfs/$f.c" -o "$out/$f.o"
    done
    cc "${CFLAGS[@]}" -I"$test_dir/fatfs_fakes" -I"$fatfs" -I"$lib_dir" \
        -c "$lib_dir/sdcard_diskio.c" -o "$out/sdcard-diskio.o"
    c++ "${CXXFLAGS[@]}" -Wno-unused-parameter -fno-rtti -I"$test_dir/fatfs_fakes" -I"$test_dir/sdcard_fakes" -I"$fatfs" \
        -I"$lib_dir" -isystem "$libs" \
        "$libs/AP_Filesystem/AP_Filesystem_RTL8735B.cpp" "$libs/AP_Filesystem/AP_Filesystem_backend.cpp" \
        "$test_dir/sdcard_fatfs_host_test.cpp" "$out/sdcard-shim.o" "$out/sdcard-diskio.o" \
        "$out/ff.o" "$out/ffunicode.o" -o "$out/sdcard-fatfs-test"
    timeout 120 "$out/sdcard-fatfs-test"
}

rc=0
for suite in uart bus io rc_storage scheduler sdcard; do
    [ -z "$only" ] || [ "$only" = "$suite" ] || continue
    # 子殼層不放在 if 或 && 裡：那樣 set -e 會失效，前面的步驟失敗也不會中止
    set +e
    ( set -e; "run_$suite" )
    status=$?
    set -e
    if [ "$status" -eq 0 ]; then
        echo "PASS hal/$suite"
    else
        echo "FAIL hal/$suite"
        rc=1
    fi
done
exit "$rc"
