# 馬達輸出

[English](motor-outputs.md)

HAL 在 PWM 輸出上支援的輸出種類與各自的限制。各自的現況見[功能狀態](feature-status.zh-TW.md)。哪幾支腳是輸出，見各板子的 README，例如 HUB 8735 ultra 的 [PWM 輸出](../hwdef/hub8735ultra/README.zh-TW.md#pwm-輸出)。DShot 為什麼由 GPIO 送、主迴圈停住時輸出為什麼停止，見[設計說明](design.zh-TW.md)。

## 輸出種類

| `MOT_PWM_TYPE` | 輸出 |
|---|---|
| 0 Normal、8 PWMRange、9 PWMAngle | `RC_SPEED` 50–490 Hz，脈寬 400–2100 µs |
| 1 OneShot | 同上的連續 PWM（晶片沒有單發觸發） |
| 2 OneShot125 | 脈寬 125–250 µs（1000–2000 除以 8），每階 0.25 µs；固定 1 kHz 連續輸出，不看 `RC_SPEED` |
| 3 Brushed | `RC_SPEED` 10–32 kHz |
| 4 DShot150、5 DShot300、6 DShot600 | 以 GPIO 送幀，各路同時；見〈DShot〉 |

- 有刷與 OneShot125 共用 PWM 的時脈（有刷用 40 MHz，OneShot125 用晶片內部的 4 MHz 振盪器），同一次開機只能用其中一種；另一種停止輸出，並以 STATUSTEXT 送出原因。
- 不支援：DShot1200（7），以及 LED 輸出（NeoPixel、ProfiLED）。
- 裝上 ESC 後先做 ESC 校準。

## 主迴圈停住時

- 解鎖中主迴圈超過 100 ms 沒跑完一圈時（例如卡住），所有 PWM 輸出停止：一般 PWM 不再送脈波，有刷馬達 duty 0。電調收不到訊號而停機，有刷馬達停轉，伺服也跟著放鬆。
- DShot 補送的油門改成 0，停住前排隊的 DShot 命令也不再送。
- 停住前的值，包括寫了還沒送出的值，都不再用。主迴圈恢復後，只有主程式重新寫入的通道以新值恢復；空的輸出不會讓舊值復活。

## DShot

- DShot 幀由 CPU 直接寫 GPIO 埠的設定與清除暫存器送出，各路同時送（`dshot_shim.c`）。
- 每送一幀關一次中斷：DShot150 約 107 µs、DShot300 約 53 µs、DShot600 約 27 µs。建議用 DShot300。
- 送幀至少 800 Hz，相鄰兩幀最多隔約 1.25 ms。主程式每次輸出送一幀，計時器每 1 ms 檢查一次，距上一幀超過 250 µs 就補送上一幀。Copter 主迴圈 400 Hz 時約每秒 1,300 幀，關中斷的時間推算約佔 DShot150 14%、DShot300 7%、DShot600 3.5%。`SERVO_DSHOT_RATE` 不用。
- 第一次切到 DShot 時以 CPU 的週期計數器量 CPU 頻率。量不到或不是 500 MHz 就不送 DShot，並以 STATUSTEXT 送出原因。
- 一幀送到一半，週期計數器停住、倒退，或跳得太遠（例如被關不掉的中斷拖住）時，腳全部拉低、這一幀作廢，下一次輸出照常送。第一次發生時以 STATUSTEXT 向地面站提示一次。0 位元的下降邊緣最多只容許晚 50 ns。
- 所有 DShot 輸出要在同一個 GPIO 埠。HUB 8735 ultra 排針上的 PWM 腳都在埠 F。
- 一路切到 DShot 之後，本次開機內那支腳維持 GPIO。改 `MOT_PWM_TYPE` 要重開機。
- 開機時的 `RCOut:` 訊息顯示各路的輸出模式，例如 `RCOut: DS300:1-4`。
- 支援：經 `send_dshot_command` 送的 DShot 命令（例如由電調發出的嗶聲），以及 BLHeli_S 的位寬（`SERVO_DSHOT_ESC`）。
- 不支援：雙向 DShot（轉速回讀）、3D 與反轉的通道設定（`SERVO_BLH_3DMASK`、`SERVO_BLH_RVMASK`）、經飛控刷電調韌體（BLHeli passthrough）。設了 `SERVO_BLH_3DMASK` 的通道固定送 0（不轉）並說明一次：照一般油門送會是約一半的正轉。

## 電調遙測

- BLHeli_32、AM32 等電調的遙測線（TLM）接到一個序列埠的 RX，只要接 RX。該埠的 `SERIALn_PROTOCOL` 設 16，鮑率自動用 115200。
- `MOT_PWM_TYPE` 要是 DShot，馬達極數設在 `SERVO_BLH_POLES`。
- 電調的電流與電壓可以用 `BATTn_MONITOR` 9 當成電池監測，也記在 `ESC` 記錄與 MAVLink 的 `ESC_TELEMETRY` 訊息。
- 不需要的自訂板，在疊加檔以 `undef HAL_SUPPORT_RCOUT_SERIAL` 拿掉。
