# 參與

Issues and pull requests are welcome here, in Chinese or English. This is not the ArduPilot project; please do not take RTL8735B issues to ArduPilot.

## 回報問題

開 issue 時選對應的範本，盡量附上：

- 板子與板型（`--board`），有疊加檔的話附上它（拿掉帳密）；
- 本儲存庫的提交（`git rev-parse --short HEAD`）；
- 建置指令，以及出錯的那段輸出或 LOG UART 的開機訊息。

**不要附含 Wi-Fi 帳密的映像或設定檔。** 帳密是編進韌體的，拿到映像就可能讀得出來。

「我照 README 做、在某塊板子上成功了」也值得回報，功能狀態表就是靠這些更新的。

## 送 PR

- 照 ArduPilot 的慣例：一個提交只動一個子系統（例如 `AP_HAL_RTL8735B:`、`hwdef:`、`waf:`、`Tools:`），提交訊息用英文寫成 `Subsystem: summary`。
- README 與程式註解用台灣用語的繁體中文。
- 本儲存庫沒有開 CI，送之前請自己建置一次：

  ```bash
  ./waf configure --board hub8735ultra
  ./waf copter
  ```

- 在 PR 裡寫清楚在哪塊板子上試過、怎麼試的；只建置過也直接寫「只建置」。
- 範圍只到 HAL，以及讓這顆晶片接上 ArduPilot 所必要的部分；特定機體的功能請放在自己的專案。
- 對 ArduPilot 共用程式（HAL 以外）的修改盡量少，要改的話寫進 HAL README 的〈對 ArduPilot 共用程式的修改〉。
- 程式改寫自其他公開來源時，在檔頭註明出處與固定的提交。
