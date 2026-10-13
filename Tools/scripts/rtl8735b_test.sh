#!/usr/bin/env bash
# RTL8735B 移植的測試：主機測試不需要板子，也不需要 Realtek SDK。
# 用法：Tools/scripts/rtl8735b_test.sh [--build]
#   --build  另外交叉建置一次 hub8735ultra 的 copter（要 SDK 與 Realtek ASDK，見 HAL README〈取得與建置〉）
# 任何一項失敗就以非 0 結束；最後印每一項的結果。說明見 libraries/AP_HAL_RTL8735B/tests/README.md。
set -uo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
tests="$root/libraries/AP_HAL_RTL8735B/tests"
build=0
for arg in "$@"; do
    case "$arg" in
        --build) build=1 ;;
        -h|--help) sed -n '2,5p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "不認得的參數：$arg（用 --help 看用法）" >&2; exit 2 ;;
    esac
done

results=()
rc=0
run() {
    local name=$1
    shift
    echo "== $name"
    if "$@"; then
        results+=("PASS $name")
    else
        results+=("FAIL $name")
        rc=1
    fi
}

for t in test_rtl8735b_hwdef_pins test_rtl8735b_flash_layout test_sdk_extract test_rtl8735b_upload; do
    run "$t" python3 -W error::ResourceWarning "$tests/$t.py"
done
run hal bash "$tests/hal/run.sh"
run hal_wifi bash "$tests/hal_wifi/run.sh"
if [ "$build" = 1 ]; then
    run build bash -c 'cd "$1" && ./waf configure --board hub8735ultra && ./waf copter' _ "$root"
fi

echo
printf '%s\n' "${results[@]}"
exit "$rc"
