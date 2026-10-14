# 上板驗證紀錄

README〈功能狀態〉標「板上驗證」的項目，都在這裡有一筆紀錄。每筆綁定建置時的提交，只增不改；結果被推翻時加一筆新的，舊的留著並註明被哪一筆取代。主機測試與只建置的結果不寫在這裡。

原始 log 不在儲存庫裡，紀錄只摘錄關鍵的幾行。

### T1 沒有感測器的開機與 Wi-Fi 連線

- 日期：2026-10-13
- 提交：`ebcbb5a8f6`
- 建置：`./waf configure --board hub8735ultra`、`./waf copter`；`RTL8735B_WIFI_CONFIG` 指向本機的 Wi-Fi 設定檔（見〈Wi-Fi〉）；`FLASH_LAYOUT` 預設；SDK `d1b6426`、Realtek ASDK 10.3.0
- 映像：`flash_ntz.bin` sha256 `86b4c8721db799a9a01be1f59075b59cdee7c6d0d39199781889f685343a8b1d`（含 Wi-Fi 帳密，別人建不出同一個值）；Flash 版面 `realtek-ota`；Wi-Fi UDP，本機埠 14555
- 開發板：HUB 8735 ultra
- 接線與感測器：沒有接任何感測器；USB 供電；沒有接馬達與電調
- 步驟：
  1. 以序列埠手動燒錄（見〈手動燒錄〉）。
  2. 錄 LOG UART，按 reset。
- 預期：開機、連上 Wi-Fi；沒有氣壓計，所以停在氣壓計的 Config Error。
- 結果：通過（只限上面兩項）。關鍵輸出：
  ```
  download success
  WiFi: joining (SSID length 20)
  WiFi: up, local 192.168.137.216:14555
  Config Error: Baro: unable to initialise driver
  ```
  `Config Error` 每 5 秒重印一次。這時 ArduPilot 停在氣壓計初始化，還沒建立 MAVLink 通道（`ArduCopter/system.cpp` 的 `init_ardupilot` 先 `barometer.init()` 再 `gcs().setup_uarts()`），所以沒有 Wi-Fi MAVLink。
- 怎麼確認對象是這塊開發板：只用這塊開發板的序列埠，沒有做網路檢查。
- 沒測到的：Wi-Fi MAVLink、參數保存、TCP、`--upload`，以及所有感測器。

### T2 範例疊加檔：感測器、Wi-Fi MAVLink（UDP）、參數保存、姿態

- 日期：2026-10-14
- 提交：`0320831602`
- 建置：`./waf configure --board hub8735ultra --extra-hwdef <本機疊加檔>`、`./waf copter`。本機疊加檔第一行 `include` 範例疊加檔 `hwdef/hub8735ultra/sensors-example.dat`，另外把 `HAL_RTL8735B_WIFI_UDP_LOCAL_PORT` 改成 14557、`MAV_SYSTEM_ID` 定成 3，沿用受測板原本的設定，和感測器無關。`RTL8735B_WIFI_CONFIG` 指向本機的 Wi-Fi 設定檔；`FLASH_LAYOUT` 預設；SDK `d1b6426`、Realtek ASDK 10.3.0
- 映像：`flash_ntz.bin` sha256 `cff481f131dbdb113cdd4cc84898cec36d75427d34d430e1577ce38d687d3cbe`（含 Wi-Fi 帳密，別人建不出同一個值）；Flash 版面 `realtek-ota`；Wi-Fi UDP，本機埠 14557、地面站埠 14550
- 開發板：HUB 8735 ultra
- 接線與感測器：照〈接上感測器〉，GY-BMI160（SPI）、BMP280、GY-271（QMC5883L），都焊在板上；BMI160 元件面朝上，前方是 USB 接頭的另一端。USB 供電；沒有接馬達、電調與電池
- 步驟：
  1. 以序列埠手動燒錄，錄 LOG UART 的開機訊息。
  2. 地面站工具在電腦上收 UDP 14550，只接受這塊開發板 IP 的封包：先收心跳 10 秒，再下載全部參數。同一段時間繼續錄 LOG UART。
  3. 把 `PILOT_SPD_UP` 從 2.5 寫成 3，等 10 秒，送重開機指令，重開後讀回，最後寫回 2.5。同一段時間錄下重開機的完整開機訊息。
  4. 請飛控以 10 Hz 送 `ATTITUDE`，錄 45 秒：先水平靜止，再把前方抬起約 45°、放平，再把後方抬起約 45°、放平。
- 預期：開機沒有 `Config Error`；三個感測器都被偵測到（`INS_ACC_ID`、`BARO1_DEVID`、`COMPASS_DEV_ID` 都不是 0）；參數在重開機後保留；水平時 roll、pitch 接近 0，前方抬起時 pitch 為正、後方抬起時為負。
- 結果：通過。關鍵輸出：
  - 開機（步驟 3 的重開機，完整錄到）：`Config Error` 出現 0 次。
    ```
    WiFi: up, local 192.168.137.49:14557
    WiFi: ground station 192.168.137.1:14550
    QMC5883L found on bus 1 id 855305 address 0x0d
    ```
  - 心跳：10 秒 10 則，系統編號 3，未解鎖。參數：927 筆全部收到，`INS_ACC_ID` 589834、`BARO1_DEVID` 226825、`COMPASS_DEV_ID` 855305。
  - 參數保存：寫入 3，重開機後讀回 3，最後寫回 2.5。
  - 姿態：

    | 時段 | roll | pitch |
    |---|---|---|
    | 水平靜止 | 1.4° | 0.3° |
    | 前方抬起 | 約 5° | 41° |
    | 後方抬起 | 約 7° | −46° |
    | 放回水平 | 1.4° | 0.2° |

- 怎麼確認對象是這塊開發板：
  1. 檢查工具只接受 192.168.137.49 的封包，這個 IP 取自同一塊開發板序列埠印出的 `WiFi: up, local 192.168.137.49:14557`；
  2. 步驟 2 期間，同一塊開發板的序列埠印出 `WiFi: ground station 192.168.137.1:14550`（電腦的 IP）；
  3. 心跳的系統編號是 3，是這塊開發板專用的值。
- 同一天的其他結果（照實記錄）：
  - 前一版的範例疊加檔（IMU 寫 `ROTATION_NONE`）在同一塊板上：開機、心跳、參數、參數保存都通過，但水平放著時 roll 讀到約 180°，表示上下顛倒。依實測改成 `ROTATION_ROLL_180_YAW_270`，就是上面的提交。
  - 提交 `0320831602` 的映像燒錄後，第一次開機出現 `Config Error: Baro: unable to initialise driver`。之後按 reset 一次、以 MAVLink 重開機一次，氣壓計都正常。接線是焊死的。原因未明；和〈功能狀態〉I²C 列記的「開機時氣壓計偶發初始化失敗」是同樣的現象，這是推論，沒有查證。
- 沒測到的：
  - roll 的正負：只測了前方、後方抬起；roll 的方向由元件面朝上與 pitch 的結果推得，屬於推論；
  - 羅盤的方向與校正（只確認有被偵測到）、加速度計校正、yaw；
  - 斷電後參數是否保留（只測了重開機）；
  - 主迴圈頻率；
  - TCP、`--upload`、飛行、馬達與 PWM 輸出、RC 輸入、GPS、長時間穩定性。
