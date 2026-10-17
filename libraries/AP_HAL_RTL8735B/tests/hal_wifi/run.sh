#!/usr/bin/env bash
# Wi-Fi 鏈路的主機測試：帳密產生器、SDK C 包裝（替身 SDK）、WiFiDriver 的 UDP 與 TCP（替身 shim 與 task）。
# 不開無線電、不開序列埠。HAL 原始碼預設是本目錄上兩層的 libraries/AP_HAL_RTL8735B，可用 $1 指定。
set -euo pipefail
ulimit -c 0
lib_dir=$(cd "${1:-$(dirname "${BASH_SOURCE[0]}")/../..}" && pwd)
test_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
out=$(mktemp -d "${TMPDIR:-/tmp}/rtl8735b-wifi-test.XXXXXX")
trap 'rm -rf -- "$out"' EXIT

RTL8735B_HAL="$lib_dir" python3 "$test_dir/test_wifi_config.py"

cc -std=c11 -Wall -Wextra -Werror -I"$test_dir/wifi_sdk_fakes" -I"$lib_dir" \
    "$lib_dir/wifi_shim.c" "$test_dir/wifi_shim_host_test.c" -o "$out/wifi-shim-test"
timeout 30 "$out/wifi-shim-test"

# 真的 RingBuffer 與 mavlink_packetise（AP_HAL/utility）；AP_HAL、FreeRTOS、MAVLink 常數用替身。
for ssid_len in 7 0; do
    cc -std=c11 -Wall -Wextra -Werror -DWIFI_TEST_SSID_LEN="$ssid_len" -I"$lib_dir" \
        -c "$test_dir/wifi_test_credentials.c" -o "$out/credentials-$ssid_len.o"
    c++ -std=c++17 -Wall -Wextra -Werror -pthread \
        -I"$test_dir/wifi_fakes" -I"$lib_dir/.." -I"$lib_dir" \
        "$lib_dir/WiFiDriver.cpp" \
        "$lib_dir/StatusText.cpp" \
        "$lib_dir/../AP_HAL/utility/RingBuffer.cpp" \
        "$lib_dir/../AP_HAL/utility/packetise.cpp" \
        "$test_dir/wifi_driver_host_test.cpp" \
        "$out/credentials-$ssid_len.o" \
        -o "$out/wifi-driver-test-$ssid_len"
    timeout 60 "$out/wifi-driver-test-$ssid_len"
done
