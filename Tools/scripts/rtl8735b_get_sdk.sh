#!/usr/bin/env bash
# 取得建置 RTL8735B 需要的 Realtek SDK 與工具鏈。
# 用法：Tools/scripts/rtl8735b_get_sdk.sh [SDK目錄]
# 預設 SDK 目錄是原始碼樹旁的 ../ambpro2_sdk（waf 的預設位置；也可用環境變數 AMEBAPRO2_SDK 指定）。
set -euo pipefail

SDK_URL=https://github.com/ambiot/ambpro2_sdk.git
SDK_COMMIT=d1b6426bb69339a609df4a3b7cd0059392999ddf
TOOLCHAIN=asdk-10.3.0-linux-newlib-build-3633-x86_64.tar.bz2
TOOLCHAIN_SHA256=2f083e6f91a41399d7a2d4ce2549bf495b2b02432d5dde2046071dddf1d8d636
TOOLS="checksum.linux elf2bin.linux gen_snrlst.linux"

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SDK=${1:-$ROOT/../ambpro2_sdk}

if [ ! -d "$SDK/.git" ]; then
    git clone -c core.autocrlf=false --no-checkout "$SDK_URL" "$SDK"
    git -C "$SDK" -c advice.detachedHead=false checkout -q --detach "$SDK_COMMIT"
fi
git -C "$SDK" cat-file -e "$SDK_COMMIT^{commit}" 2>/dev/null || git -C "$SDK" fetch origin
if [ "$(git -C "$SDK" rev-parse HEAD)" != "$SDK_COMMIT" ]; then
    # 只忽略執行位元的差異（下方 chmod 造成的），其他修改一律不切換
    if [ -n "$(git -c core.fileMode=false -C "$SDK" status --porcelain --untracked-files=no)" ]; then
        echo "SDK 有未提交的修改，不切換版本：$SDK" >&2
        exit 1
    fi
    git -C "$SDK" -c advice.detachedHead=false checkout -q --detach "$SDK_COMMIT"
fi

# SDK 的 Linux 建置工具在版本庫中沒有執行位元；只補執行位元，不改內容
for t in $TOOLS; do
    chmod +x "$SDK/project/realtek_amebapro2_v0_example/GCC-RELEASE/mp/$t"
done

parts=("$SDK/tools/$TOOLCHAIN".*)
[ -e "${parts[0]}" ] || { echo "找不到 $SDK/tools/$TOOLCHAIN.*" >&2; exit 1; }
sum=$(cat "${parts[@]}" | sha256sum | cut -d' ' -f1)
[ "$sum" = "$TOOLCHAIN_SHA256" ] || { echo "工具鏈雜湊不符：$sum" >&2; exit 1; }
BIN="$SDK/tools/asdk-10.3.0/linux/newlib/bin"
if [ ! -x "$BIN/arm-none-eabi-gcc" ]; then
    cat "${parts[@]}" | tar -xjf - -C "$SDK/tools"
fi
"$BIN/arm-none-eabi-gcc" --version | head -1
echo "完成。建置前執行：export PATH=\"$BIN:\$PATH\""
