# ArduPilot RTL8735B 移植

Unofficial ArduPilot port to the Realtek RTL8735B SoC. Documentation is in Traditional Chinese.

本儲存庫是 [ArduPilot](https://github.com/ArduPilot/ardupilot) 加上 Realtek RTL8735B 硬體抽象層（HAL）的非官方移植，由個人維護，與 ArduPilot 開發團隊及 Realtek 無關。**未經飛行驗證，請勿用於飛行。**

- 以 ArduPilot master（2026-09-25）為基線，保留 ArduPilot 的完整歷史，之後是本移植的提交。
- 需求、建置、燒錄、自訂板與功能狀態：[libraries/AP_HAL_RTL8735B/README.md](/libraries/AP_HAL_RTL8735B/README.md)
- 對 ArduPilot 共用程式的修改：同一份 README 的〈對 ArduPilot 共用程式的修改〉。
- 本平台的問題請在本儲存庫回報，不要回報到 ArduPilot 的論壇或 issue。

## 授權

ArduPilot 與本移植的程式碼依 GPL-3.0-or-later 授權，見 [COPYING.txt](/COPYING.txt)。建置需要的 Realtek SDK 另有授權條款，不在本儲存庫內，需自行取得；本儲存庫不提供韌體映像。詳見 HAL README 的〈授權〉。
