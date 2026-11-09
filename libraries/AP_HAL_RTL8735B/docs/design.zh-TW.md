# 設計說明

[English](design.md)

本移植為什麼這樣運作：和 ArduPilot 在其他平台上不同，或從設定看不出原因的地方。設定、限制與狀態本身，見各節連到的參考文件。

## Wi-Fi MAVLink 要等氣壓計

ArduPilot 在載具程式開始之前，先建立 SERIAL0（主控台）的 MAVLink 通道（`libraries/AP_Vehicle/AP_Vehicle.cpp` 中 `AP_Vehicle::setup` 的 `setup_console`）。接著 Copter 初始化氣壓計，之後才建立 Wi-Fi 等其他 SERIALn 的 MAVLink 通道（`ArduCopter/system.cpp` 的 `init_ardupilot`）。

找不到氣壓計時，開機在 Wi-Fi 通道建立之前就停在 `Config Error: Baro: unable to initialise driver`。Wi-Fi 仍會連上熱點，但 UDP 與 TCP 都不會送出或接收 MAVLink；這個錯誤本身送到 SERIAL0 上的地面站。除了 `hub8735ultra-expansion`，板型本身都沒有感測器，所以要等疊加檔至少加上氣壓計，地面站才連得上 Wi-Fi。見 [Wi-Fi](wifi.zh-TW.md) 與[描述自己的開發板](custom-boards.zh-TW.md)。

## SERIAL0 只送 MAVLink，不送文字

SERIAL0 是晶片的 LOG UART，也就是 SDK 印主控台文字的那個埠。HAL 在初始化時把它接手給 MAVLink，之後 SDK 與 HAL 用 printf 印的文字就不送出，免得夾在 MAVLink 封包之間。HAL 的執行期訊息（Wi-Fi 連線的各步驟、馬達輸出模式等）改以 STATUSTEXT 送給地面站。HAL 接手之前印的開機訊息照樣是文字。對開發板的影響，以及怎麼關掉 SERIAL0，見各板子的 README，例如 HUB 8735 ultra 的 [UART 對應](../hwdef/hub8735ultra/README.zh-TW.md#uart-對應)。

## DShot 由 GPIO 送出

HUB 8735 ultra 排針上的四路 PWM 輸出 PF11–PF14 是晶片的 PWM5–PWM8（SDK `d1b6426` 的 `component/mbed/targets/hal/rtl8735b/pwmout_api.c:77-78` 腳位表），而這版 SDK 只有 PWM0–PWM3 的 GDMA handshake（`component/soc/8735b/fwlib/rtl8735b/include/rtl8735b_gdma.h:69-72`）。沒有 DMA，PWM 硬體沒辦法在 DShot 幀的每個位元換脈寬。所以 DShot 幀由 CPU 自己送：直接寫 GPIO 埠的設定與清除暫存器，各路同時送（`dshot_shim.c`）。

因此有三件事：

- 送一幀時關中斷，免得中斷把位元拉長；DShot 速率越慢，每幀關中斷的時間越長。
- 所有 DShot 輸出要在同一個 GPIO 埠，因為一次寫暫存器就設定全部。
- 位元的寬度以 CPU 的週期計數器計時，所以第一次送 DShot 之前先量 CPU 頻率，不是時序所假設的 500 MHz 就不送 DShot。

送幀至少 800 Hz，因為電調要收到連續的幀：ChibiOS 也以 800 Hz 為下限，BLHeli_32 在約 732 Hz 以下有問題。主迴圈較慢時，由計時器補送上一幀。

0 位元的下降邊緣太晚，可能被讀成 1。Bluejay 與 BLHeli_S 的 0／1 分界只比 0 位元長約 60 ns（由原始碼推算，所用版本：bird-sanctuary/bluejay `0368d114`、bitdump/BLHeli `80ac909b` 的 BLHeli_S Rev16.7），所以本移植只容許邊緣晚 50 ns，週期計數器顯示會更晚時就作廢那一幀。

數字與限制見[馬達輸出](motor-outputs.zh-TW.md)。

## 有刷與 OneShot125 共用時脈

有刷輸出與 OneShot125 用同一個 PWM 時脈，來源不同：有刷馬達用 40 MHz 產生 10–32 kHz 的輸出，OneShot125 用晶片內部的 4 MHz 振盪器取得 0.25 µs 的刻度。同一次開機只能用其中一種，另一種停止輸出，並以 STATUSTEXT 向地面站說明原因。見[馬達輸出](motor-outputs.zh-TW.md)。

## 主迴圈停住時輸出停止

電調一直收到同樣的脈波，就一直維持那個油門。主迴圈停住（例如卡住）時，最後的值會一直重複下去。所以解鎖中主迴圈超過 100 ms 沒跑完一圈時，HAL 停止所有 PWM 輸出，DShot 改送油門 0：電調收不到訊號而停機，有刷馬達停轉，伺服也跟著放鬆。

停住前的值，包括寫了還沒送出的值，都不再用。主迴圈恢復後，只有它重新寫入的通道以新值恢復，不再寫入的通道不會帶著舊值回來。見[馬達輸出](motor-outputs.zh-TW.md)。

## Wi-Fi 的 UDP 湊包與重試

送給地面站的 MAVLink 是許多小訊息。每則各送一個資料包，資料包就很多，所以驅動把幾則完整的訊息湊進同一個資料包，最多等 10 ms 湊滿。資料包數降到幾分之一，代價是每則訊息最多晚約 10 ms。

Wi-Fi 驅動的封包緩衝所有 socket 共用，影像串流等流量多時會暫時用完，這時送出失敗，錯誤是 `ENOBUFS` 或 `ENOMEM`。湊好的資料包裡可能有十幾則訊息，所以驅動不立刻丟掉，而是留著再試，最多 500 ms。這段時間指令與參數回覆排在它後面。見 [Wi-Fi](wifi.zh-TW.md)。

## 外接羅盤預設不自動搜尋

ArduPilot 可以在開機時，於 `external` 的 I²C 匯流排上找所有編進韌體的羅盤型號（`AP_COMPASS_PROBING_ENABLED`）。這個搜尋在 hwdef 的 `COMPASS` 列之前執行，找到的羅盤以驅動的預設方向登錄（`libraries/AP_Compass/AP_Compass.cpp` 的 `_detect_backends`），同一個位址在 `COMPASS` 列寫的方向就不生效。板型不打開自動搜尋，以疊加檔的 `COMPASS` 列為準。見[描述自己的開發板](custom-boards.zh-TW.md)。

## 參數儲存與斷電

參數用 ArduPilot 的 `AP_FlashStorage` 存在兩個 64 KiB 的 Flash 扇區之間輪替。寫到一半斷電，只會丟掉最後那一筆。一個扇區寫滿時換到另一個，寫滿的那個要先抹除才能再用。

本移植在下一次開機時抹除，共 16 次 4 KiB 抹除。這段時間斷電或按 reset，再開機時參數會全部回到預設。換扇區不常發生，所以照常用地面站存的參數檔就能復原。

執行中也可能要抹除（同一次開機裡第二次換扇區，或寫入失敗後重建參數區）。抹除只在上鎖時開始；進行中解鎖前檢查報 `Param storage failed`，一般解鎖被擋，抹完才恢復。見 [Flash 版面](flash-layout.zh-TW.md)。

## 序列埠沒有反相

SDK `d1b6426` 的 UART 暫存器只有 IrDA 脈波用的反相位元（`rtl8735b_uart_type.h` 的 `irda_tx_inv`、`irda_rx_inv`），也沒有單線模式。本移植據此認定一般模式下不能反相、不能半雙工；上板收得到反相的 8N1 資料就能推翻。SBUS 是反相訊號，要經外接反相器。ArduPilot 試 SBUS 時要求反相，HAL 回報不支援，但埠照樣以 8E2 開著，收得到反相器轉回來的資料。

## I²C 匯流排編號依 SDK

每一行 `RTL8735B_I2CBUS` 寫的是 SDK 的 I2C 控制器（I2C0–I2C3），因為各控制器能用哪些腳由 SDK 決定（`component/mbed/targets/hal/rtl8735b/i2c_api.c`）。板商 Arduino variant 的標示可能不同：HUB 8735 ultra 的 `variant.h` 把 PE4／PE3 標成 I2C0、PA1／PA0 標成 I2C2，和 SDK 相反。見 [hwdef 設定](hwdef.zh-TW.md)。
