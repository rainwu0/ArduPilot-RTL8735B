/*
 * This file is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <AP_HAL/AP_HAL.h>
#if CONFIG_HAL_BOARD == HAL_BOARD_RTL8735B

#include <AP_HAL_Empty/AP_HAL_Empty_Private.h>
#include <AP_BoardConfig/AP_BoardConfig.h>
#include <AP_InternalError/AP_InternalError.h>

#include "HAL_RTL8735B_Class.h"
#include "Scheduler.h"
#include "Semaphores.h"
#include "UARTConfig.h"
#include "UARTDriver.h"
#include "Util.h"
#include "RCInput.h"
#include "Storage.h"
#include "I2CDevice.h"
#include "SPIDevice.h"
#include "GPIO.h"
#include "AnalogIn.h"
#include "RCOutput.h"
#include "WiFiDriver.h"
#include "wdt_shim.h"

// UART1（SDK LOG）保留給 SDK；UART2 遙測、UART0 GPS、UART3 序列 RC。
static RTL8735B::UARTDriver serial0Driver(0, RTL8735B_UART_CONFIGS[0].uart_id,
                                           RTL8735B_UART_CONFIGS[0].tx_pin,
                                           RTL8735B_UART_CONFIGS[0].rx_pin,
                                           RTL8735B_UART_CONFIGS[0].enabled);
static RTL8735B::UARTDriver serial1Driver(1, RTL8735B_UART_CONFIGS[1].uart_id,
                                           RTL8735B_UART_CONFIGS[1].tx_pin,
                                           RTL8735B_UART_CONFIGS[1].rx_pin,
                                           RTL8735B_UART_CONFIGS[1].enabled);
static RTL8735B::UARTDriver serial2Driver(2, RTL8735B_UART_CONFIGS[2].uart_id,
                                           RTL8735B_UART_CONFIGS[2].tx_pin,
                                           RTL8735B_UART_CONFIGS[2].rx_pin,
                                           RTL8735B_UART_CONFIGS[2].enabled);
static RTL8735B::UARTDriver serial3Driver(3, RTL8735B_UART_CONFIGS[3].uart_id,
                                           RTL8735B_UART_CONFIGS[3].tx_pin,
                                           RTL8735B_UART_CONFIGS[3].rx_pin,
                                           RTL8735B_UART_CONFIGS[3].enabled);
static RTL8735B::UARTDriver serial4Driver(4, RTL8735B_UART_CONFIGS[4].uart_id,
                                           RTL8735B_UART_CONFIGS[4].tx_pin,
                                           RTL8735B_UART_CONFIGS[4].rx_pin,
                                           RTL8735B_UART_CONFIGS[4].enabled);
static RTL8735B::UARTDriver serial5Driver(5, RTL8735B_UART_CONFIGS[5].uart_id,
                                           RTL8735B_UART_CONFIGS[5].tx_pin,
                                           RTL8735B_UART_CONFIGS[5].rx_pin,
                                           RTL8735B_UART_CONFIGS[5].enabled);
static RTL8735B::UARTDriver serial6Driver(6, RTL8735B_UART_CONFIGS[6].uart_id,
                                           RTL8735B_UART_CONFIGS[6].tx_pin,
                                           RTL8735B_UART_CONFIGS[6].rx_pin,
                                           RTL8735B_UART_CONFIGS[6].enabled);
static RTL8735B::UARTDriver serial7Driver(7, RTL8735B_UART_CONFIGS[7].uart_id,
                                           RTL8735B_UART_CONFIGS[7].tx_pin,
                                           RTL8735B_UART_CONFIGS[7].rx_pin,
                                           RTL8735B_UART_CONFIGS[7].enabled);
static RTL8735B::UARTDriver serial8Driver(8, RTL8735B_UART_CONFIGS[8].uart_id,
                                           RTL8735B_UART_CONFIGS[8].tx_pin,
                                           RTL8735B_UART_CONFIGS[8].rx_pin,
                                           RTL8735B_UART_CONFIGS[8].enabled);
static RTL8735B::UARTDriver serial9Driver(9, RTL8735B_UART_CONFIGS[9].uart_id,
                                           RTL8735B_UART_CONFIGS[9].tx_pin,
                                           RTL8735B_UART_CONFIGS[9].rx_pin,
                                           RTL8735B_UART_CONFIGS[9].enabled);
// Wi-Fi STA 上的 MAVLink：板定義指定的 SERIALn 改由 WiFiDriver 提供，該埠的實體 UART 必須停用。
// 預設 UDP；定義 HAL_RTL8735B_WIFI_TCP_PORT 時改為該埠的 TCP server。
#ifdef HAL_RTL8735B_WIFI_SERIAL
#define RTL8735B_SERIAL_ENABLED_(n) HAL_RTL8735B_SERIAL##n##_ENABLED
#define RTL8735B_SERIAL_ENABLED(n) RTL8735B_SERIAL_ENABLED_(n)
#if HAL_RTL8735B_WIFI_SERIAL < 0 || HAL_RTL8735B_WIFI_SERIAL > 9
#error "HAL_RTL8735B_WIFI_SERIAL must be 0-9"
#elif RTL8735B_SERIAL_ENABLED(HAL_RTL8735B_WIFI_SERIAL)
#error "the SERIALn used for WiFi must have its UART disabled in hwdef.dat"
#endif
#ifdef HAL_RTL8735B_WIFI_TCP_PORT
static RTL8735B::WiFiDriver wifiDriver(RTL8735B::WiFiDriver::Protocol::TCP, HAL_RTL8735B_WIFI_TCP_PORT, 0);
#else
static RTL8735B::WiFiDriver wifiDriver(RTL8735B::WiFiDriver::Protocol::UDP,
                                       HAL_RTL8735B_WIFI_UDP_LOCAL_PORT, HAL_RTL8735B_WIFI_UDP_GCS_PORT);
#endif
#endif

static AP_HAL::UARTDriver *serial_port(uint8_t index, RTL8735B::UARTDriver &uart)
{
#ifdef HAL_RTL8735B_WIFI_SERIAL
    if (index == HAL_RTL8735B_WIFI_SERIAL) {
        return &wifiDriver;
    }
#endif
    (void)index;
    return &uart;
}

static RTL8735B::I2CDeviceManager i2cDeviceManager;
static RTL8735B::SPIDeviceManager spiDeviceManager;
static RTL8735B::AnalogIn analogIn;
static RTL8735B::Storage storageDriver;
static RTL8735B::GPIO gpioDriver;
static RTL8735B::RCInput rcinDriver;
static RTL8735B::RCOutput rcoutDriver;
static RTL8735B::Scheduler schedulerInstance;
static RTL8735B::Util utilInstance;
static Empty::OpticalFlow opticalFlowDriver;
static Empty::Flash flashDriver;
#if HAL_WITH_DSP
static Empty::DSP dspDriver;
#endif

HAL_RTL8735B::HAL_RTL8735B() :
    AP_HAL::HAL(
        serial_port(0, serial0Driver), // console
        serial_port(1, serial1Driver), // telem1
        serial_port(2, serial2Driver), // telem2
        serial_port(3, serial3Driver), // GPS1
        serial_port(4, serial4Driver), // GPS2
        serial_port(5, serial5Driver), // extra1
        serial_port(6, serial6Driver), // extra2
        serial_port(7, serial7Driver), // extra3
        serial_port(8, serial8Driver), // extra4
        serial_port(9, serial9Driver), // extra5
        &i2cDeviceManager,
        &spiDeviceManager,
        nullptr,        // no WSPI
        &analogIn,
        &storageDriver,
        &serial0Driver, // console
        &gpioDriver,
        &rcinDriver,
        &rcoutDriver,
        &schedulerInstance,
        &utilInstance,
        &opticalFlowDriver,
        &flashDriver,
#if HAL_WITH_DSP
        &dspDriver,
#endif
        nullptr)        // no CAN
{}

void HAL_RTL8735B::run(int argc, char * const argv[], Callbacks* callbacks) const
{
    // 由 targets/cmake/main.c 在 FreeRTOS task 內呼叫；這個 task 就是主執行緒，
    // scheduler->init() 會另外建立 timer、IO、UART 與 storage 的 task。

    // 上一次重置的原因要在任何程式查詢 was_watchdog_reset() 之前讀取並清除（wdt_shim.c）
    (void)rtl8735b_reset_was_watchdog();

    scheduler->init();
    serial(0)->begin(115200);
    gpio->init();
    rcout->init();
    analogin->init();
    storage->init();
    rcin->init();

    callbacks->setup();

    // 硬體看門狗：setup() 之後依 BRD_OPTIONS bit0 啟動，主迴圈每圈餵狗；
    // 主迴圈停住、或優先權高於主迴圈的執行緒與中斷佔住 CPU 超過逾時，晶片才重置。
    // 本板 BRD_OPTIONS 預設 0（AP_BoardConfig 的 HAL_BRD_OPTIONS_DEFAULT 只對 ChibiOS 開），看門狗預設關閉：
    // Flash 抹寫時 CPU 是否停頓尚未量測，實際逾時與重置範圍也待
    // 板上確認（wdt_shim.c），預設開可能在抹寫時誤觸重開。
    schedulerInstance.watchdog_start(AP_BoardConfig::watchdog_enabled());
    if (util->was_watchdog_reset()) {
        INTERNAL_ERROR(AP_InternalError::error_t::watchdog_reset);
    }
    schedulerInstance.watchdog_pat();
    scheduler->set_system_initialized();

    for (;;) {
        callbacks->loop();
        schedulerInstance.watchdog_pat();
    }
}

static HAL_RTL8735B hal_rtl8735b;

const AP_HAL::HAL& AP_HAL::get_HAL()
{
    return hal_rtl8735b;
}

AP_HAL::HAL& AP_HAL::get_HAL_mutable()
{
    return hal_rtl8735b;
}

void AP_HAL::init()
{
}

#endif // CONFIG_HAL_BOARD == HAL_BOARD_RTL8735B
