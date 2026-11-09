# 開發板的應用程式

[English](board-applications.md)

自訂板可以把 SDK 端的 C 程式（開發板的應用程式）編進同一份韌體。本頁說明應用程式與韌體之間的介面；宣告與完整說明在 `app/rtl8735b_app.h`。

## 編進應用程式

在疊加的 hwdef 以一行指定目錄：

```text
env RTL8735B_APP_DIR <目錄>
```

- 相對路徑以本儲存庫根目錄為基準；目錄不存在時 configure 失敗。
- 目錄中的 `.c` 檔與 SDK 原始碼一起編譯，沿用 SDK 的 include 與巨集；目錄中沒有 `.c` 檔時建置失敗。
- 應用程式只用 SDK 與 `app/rtl8735b_app.h`，不存取 ArduPilot 的內部。task 優先權在 3 以下：ArduPilot 主執行緒為 4，見 `Scheduler.h`。
- 沒有這一行時，韌體不含任何應用程式。

## 韌體呼叫的函式

`rtl8735b_app_init` 一定要定義：沒有定義時連結失敗（`undefined reference to rtl8735b_app_init`），所以少了應用程式不會悄悄變成什麼都不做。另外三個是選用的，沒有定義時韌體用空函式，`rtl8735b_app_arming_blocked` 回傳 0。

| 函式 | 呼叫時機 | 限制 |
|---|---|---|
| `rtl8735b_app_init` | 每次開機一次：建立 ArduPilot 的 task 之後、啟動排程器之前；ArduPilot 一行都還沒執行 | 只建立 task；可以 `printf`、直接以 SDK 讀寫 Flash；要排程器才會完成的事都不能等 |
| `rtl8735b_app_after_setup` | 每次開機一次：在 ArduPilot 的主 task 中，`setup()` 完成之後、第一次主迴圈之前 | 只建立 task 或設定變數；阻塞會讓主迴圈延後開始 |
| `rtl8735b_app_arming_blocked` | 未解鎖時，在主 task 的解鎖檢查與狀態回報中呼叫；回傳非 0 就擋解鎖 | 只讀變數：不阻塞、不拿鎖 |
| `rtl8735b_app_before_reset` | 韌體自己要重置之前（`Scheduler::reboot()`，例如地面站的重開機指令、要重開才生效的參數）；呼叫時輸出已經安全，返回後就重置 | 可能在任何 task 中呼叫：不阻塞、不拿鎖、不印字 |

`rtl8735b_app_arming_blocked` 回傳非 0 時，HAL 回報安全開關未解除。Copter 不論 `ARMING_SKIPCHK` 怎麼設，一般解鎖都會被拒（顯示 `Safety Switch`），只有強制解鎖不經這項檢查。其他載具由解鎖前的安全開關檢查擋住；`ARMING_SKIPCHK` 略過這項檢查或強制解鎖時不擋。擋住期間，Copter 的馬達測試也會被拒，地面站顯示馬達輸出未啟用。它可能從不同的 task 被呼叫，只回傳一次就能讀完的變數（例如 `volatile uint8_t`）。沒有應用程式時回傳 0。

`rtl8735b_app_before_reset` 給兼作開機模式腳的 GPIO 用。重置時 PA5 的電位決定是否進入燒錄模式。應用程式拿 PA5 當輸出時（HUB 8735 ultra 的 D14，`variant.h` 標為 Sensor power on），在這裡把它放回開機時的狀態；否則韌體重開後可能停在燒錄模式、不啟動。它只涵蓋韌體自己的重開機：看門狗與當機的重置不經過這裡；應用程式自己呼叫 SDK 的 `sys_reset()` 時，自己先處理這些腳。

## 應用程式可以呼叫的函式

| 函式 | 回傳或作用 | 呼叫限制 |
|---|---|---|
| `rtl8735b_ardupilot_armed()` | 1：已解鎖（`AP_HAL::Util::get_soft_armed()`，馬達測試也算）；0：未解鎖 | 只讀變數，不阻塞、不拿鎖，任何 task 與 FreeRTOS 計時器回呼都可以呼叫 |
| `rtl8735b_ardupilot_system_initialized()` | 1：`setup()` 已完成；0：還沒 | 同上 |
| `rtl8735b_ardupilot_ms_since_main_loop()` | 距上一次主迴圈跑完的毫秒數，查詢當下才算，主迴圈卡住時照樣變大；還沒跑完過一圈時是 `UINT32_MAX` | 同上 |
| `rtl8735b_ardupilot_outputs_safe()` | 讓所有 PWM 輸出停止、安全開關回到上鎖（`RCOutput::force_safety_on()`），例如應用程式要以 `sys_reset()` 重置之前；這種重置之前載具要在上鎖狀態 | 會等主 task 放開輸出的鎖：任何 FreeRTOS task 都可以呼叫，不能從中斷或 FreeRTOS 計時器回呼呼叫 |

## 編譯定義

應用程式的 `.c` 檔另有這些定義，值依 hwdef 的 `FLASH_LAYOUT`（見 [Flash 版面](flash-layout.zh-TW.md)）：

- `RTL8735B_FLASH_LAYOUT_NAME`：版面名稱的字串，例如 `"realtek-ota"`。
- `RTL8735B_FLASH_FW1_ADDR`、`RTL8735B_FLASH_FW1_LEN`、`RTL8735B_FLASH_FW2_ADDR`、`RTL8735B_FLASH_FW2_LEN`：這份映像分割表裡 fw1、fw2 的位址與長度，單位 bytes。

韌體本身的原始碼沒有這些定義。

## `examples/AppHost`

沒有感測器的開發板，用範例 `examples/AppHost` 執行應用程式：它只開 Wi-Fi 與主迴圈，不初始化感測器、不解鎖，PWM 輸出留在安全狀態，所以 `setup()` 會結束、主迴圈照常跑。一般的載具映像在沒有 IMU 的開發板上停在慣性感測器的初始化，`rtl8735b_app_after_setup` 不會被呼叫。

```bash
./waf configure --board rtl8735b-empty --extra-hwdef /path/to/myapp.dat
./waf --targets examples/AppHost
```

開發板應用程式的狀態見[功能狀態](feature-status.zh-TW.md)。
