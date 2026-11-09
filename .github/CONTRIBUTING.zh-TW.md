# 參與

[English](CONTRIBUTING.md)

歡迎在這裡開 issue、送 PR，中文或英文都可以。本儲存庫不是 ArduPilot 專案；RTL8735B 移植的問題在這裡處理。

## 回報問題

開 issue 時選對應的範本，盡量附上：

- 開發板與板型（`--board`），有疊加檔的話附上它；
- 本儲存庫的提交（`git rev-parse --short HEAD`）；
- 建置指令，以及出錯的那段輸出，或地面站的訊息、USB 序列埠上的開機訊息。

用 Wi-Fi 設定檔建置的映像含有帳密，拿到映像的人可能讀得出來。附在 issue 的映像，不設 `RTL8735B_WIFI_CONFIG` 建置；疊加檔與設定檔拿掉帳密再附。

「我照文件做、在某塊開發板上成功了」也值得回報，功能狀態就是靠這些更新的。

安全問題，例如能讓別人控制飛控、讀出帳密，照 [SECURITY](SECURITY.zh-TW.md) 的方法私下回報。

## 送 PR

- 照 ArduPilot 的慣例：一個提交只動一個子系統（例如 `AP_HAL_RTL8735B:`、`hwdef:`、`waf:`、`Tools:`），提交訊息用英文寫成 `Subsystem: summary`。
- 送之前先跑測試。改了 HAL 或建置相關的檔案，加 `--build` 再交叉建置一次（要 SDK，見[建置韌體](/libraries/AP_HAL_RTL8735B/docs/building.zh-TW.md)）：

  ```bash
  Tools/scripts/rtl8735b_test.sh
  Tools/scripts/rtl8735b_test.sh --build
  ```

  PR 會在 GitHub Actions 跑同一套主機測試和提交慣例檢查，外部貢獻者的 PR 要等維護者核准才會開始跑；交叉建置不會自動跑。修錯誤時，先寫一個修正前會失敗的測試，見 `libraries/AP_HAL_RTL8735B/tests/README.md`。
- 在 PR 裡寫清楚在哪塊開發板上試過、怎麼試的；只建置過就寫「只建置」。
- 範圍只到 HAL，以及讓這顆晶片接上 ArduPilot 所必要的部分；特定機體的功能放在自己的專案。
- 對 ArduPilot 共用程式（HAL 以外）的修改盡量少，每一處都寫進 [HAL README](/libraries/AP_HAL_RTL8735B/README.zh-TW.md) 的〈對 ArduPilot 共用程式的修改〉。
- 程式改寫自其他公開來源時，在檔頭註明出處與固定的提交。

## 文件

- 有兩種語言的文件，英文版用預設檔名，台灣用語的繁體中文版用 `.zh-TW.md`，兩版的結構與內容一致，開頭互相連結。改其中一版時，另一版在同一個提交裡一起改。
- 程式註解、`BOARD_TESTS.md`、`tests/README.md` 用台灣用語的繁體中文。
- 放在哪裡，以 `libraries/AP_HAL_RTL8735B/` 為準：
  - 某一塊開發板的事實：`hwdef/<板型>/README.md`，章節照 ArduPilot 的板子 README；
  - 怎麼做某件事：`docs/building.md`、`docs/flashing.md`、`docs/custom-boards.md`；
  - 設定與行為：`docs/` 裡的參考文件；
  - 本移植為什麼這樣運作：`docs/design.md`；
  - 新使用者的第一次完整流程：`GETTING_STARTED.md`。
- `docs/feature-status.md` 的一列要改成「板上驗證」，`BOARD_TESTS.md` 裡要有對應的紀錄。
- 使用者看得到的改動（行為、疊加檔的寫法、參數、建置或燒錄的步驟、文件的檔名），在 `libraries/AP_HAL_RTL8735B/CHANGELOG.md` 與 `CHANGELOG.zh-TW.md` 的 `[Unreleased]` 底下加一條，分類照 Keep a Changelog（新增、變更、修正、移除等）。
