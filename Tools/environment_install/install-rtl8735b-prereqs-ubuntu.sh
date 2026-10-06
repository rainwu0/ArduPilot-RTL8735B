#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# 安裝建置 RTL8735B 需要的套件（Ubuntu 22.04 或 WSL），並建立 Python 虛擬環境（預設 ~/.venv-rtl8735b）。
set -euo pipefail

APT_PKGS="git build-essential cmake bzip2 python3 python3-venv python3-pip ccache"
missing=()
for p in $APT_PKGS; do dpkg -s "$p" >/dev/null 2>&1 || missing+=("$p"); done
if [ ${#missing[@]} -gt 0 ]; then
    sudo apt-get update
    sudo apt-get install -y "${missing[@]}"
fi

VENV=${VENV:-$HOME/.venv-rtl8735b}
[ -d "$VENV" ] || python3 -m venv "$VENV"
"$VENV/bin/pip" install --quiet --upgrade pip
"$VENV/bin/pip" install --quiet empy==3.3.4 pexpect==4.9.0 ptyprocess==0.7.0 dronecan==1.0.27 future==1.0.0 lxml==6.1.3
echo "完成。建置前執行：source $VENV/bin/activate"
