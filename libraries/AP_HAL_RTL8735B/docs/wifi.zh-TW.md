# Wi-Fi

[English](wifi.md)

HAL 怎麼用晶片的 Wi-Fi：預設熱點、設定檔、MAVLink 的埠、UDP 怎麼送、顯示的訊息，以及安全上的限制。其中一些為什麼這樣做，見[設計說明](design.zh-TW.md)。

## 預設熱點

沒有設定檔時，開發板以 station（STA）模式連到預設熱點：SSID `ardupilot`、密碼 `ardupilot1`，和 ArduPilot ESP32 HAL 程式裡的預設相同（ArduPilot `9f648ccabc` 的 `libraries/AP_HAL_ESP32/WiFiDriver.cpp`）。用手機或電腦開一個同名、同密碼的熱點，地面站連同一個熱點。

- 這組值由板型 `rtl8735b-empty` 的 `hwdef.dat` 以 `WIFI_SSID_STATION`、`WIFI_PWD` 設定；`hub8735ultra` 與 `amb82-mini` 都 include 它，`hub8735ultra-expansion` include `hub8735ultra`。
- 換成自己的熱點：給設定檔（見下一節），或在疊加檔 `undef` 後重新 `define` `WIFI_SSID_STATION`、`WIFI_PWD`。設定檔優先於疊加檔。
- `WIFI_SSID_STATION` 是 `""` 或沒有定義、又沒有設定檔時，Wi-Fi 不啟用。
- 這組值是公開的：附近任何人都能開一個同名、同密碼的熱點讓開發板連上，或連進你開的熱點，看到並送出 MAVLink。

## 設定檔

要連自己的熱點，把熱點名稱與密碼寫在本機的設定檔，建置時以環境變數 `RTL8735B_WIFI_CONFIG` 指定；沒有指定時，開發板連預設熱點。在建置中放在哪一步，見[建置韌體](building.zh-TW.md)。

```text
ssid=你的熱點名稱
password=你的熱點密碼
```

- 等號後面的內容照原樣使用：引號，以及等號前後或行尾的空白，都會變成名稱或密碼的一部分。名稱中間的空白沒關係。
- 多了引號或空白，建置時不會警告，之後才在 `WiFi: joining (SSID length N)` 看到 N 不對：N 是名稱的位元組數，英數字一個算 1，中文一個字算 3。
- 密碼是 8–63 個字元，或 64 個十六進位字元，只能用可列印的 ASCII（英文字母、數字、符號和空白）。開放網路的 `password=` 留空。長度或字元不對時建置失敗，並說明原因。
- 設定檔在建置時讀取：改了之後重新建置即可，不必重新 configure。
- 建置時用到設定檔會印 `Wi-Fi STA: 帳密取自 <路徑>`，沒有設定檔時印 `Wi-Fi STA: 未設定（…），開發板連板型的預設熱點…`。
- 帳密只寫進建置目錄，並以常數陣列編進韌體：拿到以它建置的映像的人，可能讀得出來。

## MAVLink 的埠

板型中 Wi-Fi 是 SERIAL2，預設協定是 MAVLink2。

| 傳輸 | 開發板 | 地面站 | 選擇方式 |
|---|---|---|---|
| UDP | 聽本機埠 14555。送給送封包到這個埠的地面站；還沒有地面站時，把 HEARTBEAT 廣播到子網路的 14550 埠 | 聽 14550 埠 | 預設 |
| TCP server | 聽設定的埠，例如 5760 | 連到開發板的 IP 與這個埠 | 疊加檔寫 `define HAL_RTL8735B_WIFI_TCP_PORT 5760` |

- TCP 同時只服務一個地面站。
- Wi-Fi 的 MAVLink 通道在氣壓計初始化之後才建立。沒有氣壓計時，開機停在 `Config Error: Baro: unable to initialise driver`：Wi-Fi 仍會連上熱點，但 UDP 與 TCP 都不會送出或接收 MAVLink（[原因](design.zh-TW.md#wi-fi-mavlink-要等氣壓計)）。

## UDP 怎麼送

- 幾則完整的 MAVLink 湊進同一個資料包，最多 1,472 bytes。湊不滿時最多等 10 ms，所以每則訊息最多晚約 10 ms 送出（`WiFiCoalesce.h`）。還沒有地面站時廣播的 HEARTBEAT 照舊一則一包。TCP 不受影響。
- 送出遇到 Wi-Fi 驅動的封包緩衝不足（`ENOBUFS`、`ENOMEM`）時，那一個資料包留著，下一步（約 1 ms 後）再送，從第一次失敗起最多 500 ms，到了才丟掉。其他送出失敗直接丟掉（`WiFiTxRetry.h`）。
- 重試最多卡 500 ms，期間指令與參數回覆跟著排隊；送出緩衝區 16 KiB，裝得下這段時間的 MAVLink。重試滿 500 ms 仍送不出時整個資料包丟掉，裡面可能有十幾則訊息。

## 訊息

HAL 以 STATUSTEXT 回報 Wi-Fi 的連線。Wi-Fi 還沒連上之前，只有其他連線上的地面站收得到，例如經開發板 USB 序列埠連在 SERIAL0 的地面站。

| 訊息 | 意思 |
|---|---|
| `WiFi: joining the board's default hotspot (SSID length N)` | 正在連預設熱點；N 是名稱的位元組數，`ardupilot` 是 9 |
| `WiFi: joining (SSID length N)` | 正在連設定檔裡的熱點；N 是名稱的位元組數 |
| `WiFi: up, local <IP>:<port>` | 已連上：開發板的 IP 與本機的 MAVLink 埠 |
| `WiFi: join failed (…)` | 連不上熱點；開發板會自己重試 |
| `WiFi: link lost (…), reconnecting` | 已連上的熱點斷了；開發板會自己重新連 |
| `WiFi: no STA credentials in this image; WiFi MAVLink port disabled` | 沒有設定檔，而且 `WIFI_SSID_STATION` 是 `""` 或沒有定義：Wi-Fi 不啟用 |

熱點不在或密碼錯時，每次失敗後等 1 秒再試。地面站收到一次 joining；`WiFi: join failed (…)` 在第一次、錯誤碼改變時送，之後至少相隔 30 秒才再送一次，其餘只印在主控台。連上時送 `WiFi: up, local <IP>:<埠>`，之後再斷線又從第一次算起。

## 安全上的限制

- 帳密：編進映像，見〈設定檔〉。要分享或附在 issue 的映像，可以不設 `RTL8735B_WIFI_CONFIG` 建置；這樣的映像只有[預設熱點](#預設熱點)那組值。
- 預設熱點：見[預設熱點](#預設熱點)。
- UDP 上的 MAVLink 沒有任何驗證。還沒有地面站、或地面站停止送封包超過 3 秒時，送封包到本機埠的任何來源都會被當成新的地面站，包括同網段的其他飛控或程式，它送來的 RC override 也會被接受。沒有地面站時，HEARTBEAT 廣播到整個子網路。
- TCP 上的 MAVLink 同樣沒有驗證。地面站停止送資料超過 3 秒後，同網段任一主機的新連線就會取代它。
- 在只有自己控制的網路上，其他裝置連不到開發板的 MAVLink 埠。安全問題怎麼回報，見 [SECURITY](../../../.github/SECURITY.zh-TW.md)。
