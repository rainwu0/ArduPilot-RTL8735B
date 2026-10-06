/* SPDX-License-Identifier: GPL-3.0-or-later */
/* SDK C 介面與標頭留在 HAL 邊界的 C 端。 */
#include "bus_shim.h"
#include "sdk_shim.h"

#include <diag.h>
#include <hal_i2c.h>
#include <hal_pinmux.h>
#include <hal_ssi.h>
#include <gpio_api.h>
#include <i2c_api.h>
#include <objects.h>
#include <spi_api.h>
#include <spi_ex_api.h>
#include <cmsis_compiler.h>
#include <string.h>

#include <FreeRTOS.h>
#include <task.h>

#define BUS_I2C_ID 1U
#define BUS_SPI_ID 1U
#define BUS_I2C_MAX_TRANSFER 512U
#define BUS_SPI_MAX_TRANSFER 4096U
#define BUS_MAX_TIMEOUT_MS 100U

static i2c_t i2c_object;
static uint8_t i2c_initialized;
static uint8_t i2c_faulted;
// 位址中止後重新初始化控制器時沿用的設定。
static uint8_t i2c_bus_index;
static uint32_t i2c_sda_pin;
static uint32_t i2c_scl_pin;
static uint32_t i2c_clock_hz;
static int _i2c_hw_init(void);
static uint8_t i2c_tx_scratch[BUS_I2C_MAX_TRANSFER];
static uint8_t i2c_rx_scratch[BUS_I2C_MAX_TRANSFER];

static spi_t spi_object;
static uint8_t spi_initialized;
static uint8_t spi_faulted;
static uint8_t spi_tx_scratch[BUS_SPI_MAX_TRANSFER];
static uint8_t spi_rx_scratch[BUS_SPI_MAX_TRANSFER];
static gpio_t cs_objects[4];
static uint32_t cs_pins[4];
static uint8_t cs_initialized[4];

static int _valid_transfer(const uint8_t *send, uint32_t send_len,
                           uint8_t *recv, uint32_t recv_len)
{
    return ((send_len == 0U) || (send != NULL)) &&
           ((recv_len == 0U) || (recv != NULL)) &&
           (send_len <= BUS_SPI_MAX_TRANSFER) &&
           (recv_len <= BUS_SPI_MAX_TRANSFER) &&
           (send_len + recv_len != 0U);
}

// timeout 從這個 leg 開始時計算。bus task 以 tick 輪詢、可能被高優先 task 延後喚醒；
// 共用整筆交易的期限時，前一個 leg 晚醒會讓後一個 leg 一開始就超過期限，
// 剛啟動的接收被誤判為逾時並封鎖匯流排。每次喚醒先看狀態，晚醒本身不會造成逾時。
static int _wait_i2c(uint8_t receive, uint32_t timeout_ms)
{
    hal_i2c_adapter_t *adapter = &i2c_object.i2c_adp;
    const uint64_t deadline_us = rtl8735b_systime_us() +
        (uint64_t)timeout_ms * 1000ULL;
    for (;;) {
        const uint8_t status = adapter->status;
        if (receive) {
            if (status != I2CStatusRxReady && status != I2CStatusRxing) {
                return status == I2CStatusTimeOut ? RTL8735B_BUS_TIMEOUT :
                    (adapter->err_type == I2CErrorNone && adapter->rx_dat.len == 0U ?
                     RTL8735B_BUS_OK : RTL8735B_BUS_ERROR);
            }
        } else if (status != I2CStatusTxReady && status != I2CStatusTxing) {
            return status == I2CStatusTimeOut ? RTL8735B_BUS_TIMEOUT :
                (adapter->err_type == I2CErrorNone && adapter->tx_dat.len == 0U ?
                 RTL8735B_BUS_OK : RTL8735B_BUS_ERROR);
        }
        if (rtl8735b_systime_us() >= deadline_us) {
            return RTL8735B_BUS_TIMEOUT;
        }
        // I2C1 使用中斷模式；讓出一個 tick，避免高優先 bus task 忙等餓死 main。
        vTaskDelay(pdMS_TO_TICKS(1U));
    }
}

int rtl8735b_i2c_init(uint8_t bus, uint32_t sda_pin, uint32_t scl_pin,
                       uint32_t clock_hz)
{
    if (bus != BUS_I2C_ID || sda_pin == 0U || scl_pin == 0U ||
        clock_hz < 1000U || clock_hz % 1000U != 0U || i2c_initialized) {
        return RTL8735B_BUS_INVALID;
    }
    // SDK 在位址 NACK 等中止時經 DBG_I2C_ERR 同步列印約 128 字元到 115200 baud 的 LOG UART，
    // 板上實測每次失敗交易因此耗時約 10 ms；探測不存在的裝置會拖慢開機並佔住 CPU。
    // 失敗已由本 shim 的回傳碼報告，所以只關閉 SDK 的 I2C 錯誤列印，其他模組的列印不變。
    DBG_ERR_MSG_OFF(_DBG_I2C_);

    i2c_bus_index = bus;
    i2c_sda_pin = sda_pin;
    i2c_scl_pin = scl_pin;
    i2c_clock_hz = clock_hz;
    const int result = _i2c_hw_init();
    if (result == RTL8735B_BUS_OK) {
        i2c_initialized = 1U;
    }
    return result;
}

static int _i2c_hw_init(void)
{
    memset(&i2c_object, 0, sizeof(i2c_object));
    hal_i2c_adapter_t *adapter = &i2c_object.i2c_adp;
    hal_status_t status = hal_i2c_load_default(adapter, i2c_bus_index);
    if (status != HAL_OK) {
        return RTL8735B_BUS_ERROR;
    }
    adapter->op_mode = I2CModeInterrupt;
    status = hal_i2c_init(adapter, i2c_scl_pin, i2c_sda_pin);
    if (status != HAL_OK) {
        return RTL8735B_BUS_ERROR;
    }
    if (i2c_object.i2c_adp.status != I2CStatusIdle &&
        i2c_object.i2c_adp.status != I2CStatusInitialized) {
        return RTL8735B_BUS_ERROR;
    }
    adapter->init_dat.clock = i2c_clock_hz / 1000U;
    status = hal_i2c_set_clk(adapter);
    if (status != HAL_OK) {
        (void)hal_i2c_deinit(adapter);
        return RTL8735B_BUS_ERROR;
    }
    // 硬體提供 restart capability；software flag 在每個 transaction leg 各自設定。
    hal_i2c_mst_restr_sw_ctrl(adapter, I2CDisable);
    if (hal_i2c_mst_restr_ctrl(adapter, I2CEnable) != HAL_OK) {
        (void)hal_i2c_deinit(adapter);
        return RTL8735B_BUS_ERROR;
    }
    return RTL8735B_BUS_OK;
}

int rtl8735b_i2c_transfer(uint8_t bus, uint8_t address,
                           const uint8_t *send, uint32_t send_len,
                           uint8_t *recv, uint32_t recv_len,
                           uint8_t split_transfers, uint32_t timeout_ms)
{
    if (bus != BUS_I2C_ID || address > 0x7FU ||
        !_valid_transfer(send, send_len, recv, recv_len) ||
        send_len > BUS_I2C_MAX_TRANSFER || recv_len > BUS_I2C_MAX_TRANSFER ||
        timeout_ms == 0U || timeout_ms > BUS_MAX_TIMEOUT_MS) {
        return RTL8735B_BUS_INVALID;
    }
    if (i2c_faulted) {
        // 保留 timeout 原因，讓上層不把已封鎖的控制器當成可重試 NACK。
        return RTL8735B_BUS_TIMEOUT;
    }
    if (!i2c_initialized) {
        return RTL8735B_BUS_ERROR;
    }

    hal_i2c_adapter_t *adapter = &i2c_object.i2c_adp;
    int result = RTL8735B_BUS_OK;
    const uint8_t use_repeated_start =
        send_len > 0U && recv_len > 0U && !split_transfers;

    // combined write/read 保留 bus 以產生 repeated START，其餘 leg 服從 mst_stop。
    hal_i2c_mst_restr_sw_ctrl(adapter,
        use_repeated_start ? I2CEnable : I2CDisable);
    if (send_len > 0U) {
        adapter->err_type = I2CErrorNone;
        memcpy(i2c_tx_scratch, send, send_len);
        // AP 與 Mbed HAL 的 adapter 欄位使用 7-bit address；方向另由 send/receive 選擇。
        adapter->tx_dat.addr = address;
        adapter->tx_dat.buf = i2c_tx_scratch;
        adapter->tx_dat.len = send_len;
        adapter->tx_dat.mst_stop = (recv_len == 0U || split_transfers) ? I2CEnable : I2CDisable;
        const hal_status_t status = hal_i2c_send(adapter);
        if (status == HAL_BUSY) {
            result = RTL8735B_BUS_BUSY;
            goto transfer_done;
        }
        if (status != HAL_OK) {
            result = RTL8735B_BUS_ERROR;
            goto transfer_done;
        }
        result = _wait_i2c(0U, timeout_ms);
        if (result != RTL8735B_BUS_OK) {
            goto transfer_done;
        }
    }

    // Split transaction 與所有 final/read-only leg 必須輸出其 mst_stop。
    hal_i2c_mst_restr_sw_ctrl(adapter, I2CDisable);
    if (result == RTL8735B_BUS_OK && recv_len > 0U) {
        adapter->err_type = I2CErrorNone;
        adapter->rx_dat.addr = address;
        adapter->rx_dat.buf = i2c_rx_scratch;
        adapter->rx_dat.len = recv_len;
        adapter->rx_dat.mst_stop = I2CEnable;
        const hal_status_t status = hal_i2c_receive(adapter);
        if (status == HAL_BUSY) {
            result = RTL8735B_BUS_BUSY;
            goto transfer_done;
        }
        if (status != HAL_OK) {
            result = RTL8735B_BUS_ERROR;
        } else {
            result = _wait_i2c(1U, timeout_ms);
        }
        if (result == RTL8735B_BUS_OK) {
            memcpy(recv, i2c_rx_scratch, recv_len);
        }
    }

transfer_done:
    // 錯誤或成功都關閉 software restart，避免下一筆交易繼承旗標。
    hal_i2c_mst_restr_sw_ctrl(adapter, I2CDisable);
    if (result == RTL8735B_BUS_ERROR && adapter->err_type == I2CErrorTxAbort) {
        // 板上實測：位址中止後 SDK 把狀態設回 Idle，但下一筆
        // 中斷模式傳送會停在 TxReady 直到逾時並封鎖匯流排。以 SDK API 重新初始化控制器；
        // 失敗則封鎖，讓上層看到真實狀態。
        (void)hal_i2c_deinit(adapter);
        if (_i2c_hw_init() != RTL8735B_BUS_OK) {
            i2c_faulted = 1U;
        }
    }
    if (result == RTL8735B_BUS_TIMEOUT) {
        // SDK 未提供已查證的終止介面；timeout 後保留靜態緩衝並封鎖後續交易。
        i2c_faulted = 1U;
    }
    return result;
}

int rtl8735b_spi_init(uint8_t bus, uint32_t mosi_pin, uint32_t miso_pin,
                       uint32_t sclk_pin, uint32_t ss_pin, uint32_t clock_hz, uint8_t mode)
{
    if (bus != BUS_SPI_ID || mosi_pin == 0U || miso_pin == 0U ||
        sclk_pin == 0U || ss_pin == 0U || clock_hz == 0U || spi_initialized) {
        return RTL8735B_BUS_INVALID;
    }
    memset(&spi_object, 0, sizeof(spi_object));
    // hal_ssi_init（經 spi_format 呼叫）以 PID_SPI1 註冊 CS/CLK/MISO/MOSI 四腳，NC 會註冊失敗
    // （Realtek 官方 SDK Ameba-AIoT/ameba-rtos-pro2@eb5c0907c40d，
    // component/soc/8735b/fwlib/rtl8735b/source/ram/hal_ssi.c:61–121）。傳入真實 CS 腳讓 SSI 完成初始化。
    spi_init(&spi_object, (PinName)mosi_pin, (PinName)miso_pin,
             (PinName)sclk_pin, (PinName)ss_pin);
    if (mode > 3U) {
        spi_free(&spi_object);
        return RTL8735B_BUS_INVALID;
    }
    spi_format(&spi_object, 8, mode, 0);
    // spi_format 不回報 hal_ssi_init 失敗；CS 腳若未被 SPI1 持有，解除註冊會失敗，藉此偵測。
    // 解除後由 rtl8735b_spi_set_cs 以 GPIO 註冊同一腳做軟體片選，維持跨 transfer 的 CS 語意。
    if (hal_pinmux_unregister(ss_pin, PID_SPI1) != HAL_OK) {
        spi_free(&spi_object);
        return RTL8735B_BUS_ERROR;
    }
    if (spi_object.hal_ssi_adaptor.index != 1U ||
        hal_ssi_set_sclk(&spi_object.hal_ssi_adaptor, clock_hz) != HAL_OK) {
        spi_free(&spi_object);
        return RTL8735B_BUS_ERROR;
    }
    spi_enable(&spi_object);
    spi_initialized = 1U;
    return RTL8735B_BUS_OK;
}

int rtl8735b_spi_set_frequency(uint8_t bus, uint32_t clock_hz)
{
    if (bus != BUS_SPI_ID || !spi_initialized || spi_faulted || clock_hz == 0U) {
        return RTL8735B_BUS_INVALID;
    }
    return hal_ssi_set_sclk(&spi_object.hal_ssi_adaptor, clock_hz) == HAL_OK ?
        RTL8735B_BUS_OK : RTL8735B_BUS_ERROR;
}

static int _get_cs(uint32_t pin, gpio_t **out)
{
    for (unsigned i = 0; i < 4U; i++) {
        if (cs_initialized[i] && cs_pins[i] == pin) {
            *out = &cs_objects[i];
            return RTL8735B_BUS_OK;
        }
    }
    for (unsigned i = 0; i < 4U; i++) {
        if (!cs_initialized[i]) {
            gpio_init(&cs_objects[i], (PinName)pin);
            gpio_dir(&cs_objects[i], PIN_OUTPUT);
            gpio_write(&cs_objects[i], 1);
            cs_pins[i] = pin;
            cs_initialized[i] = 1U;
            *out = &cs_objects[i];
            return RTL8735B_BUS_OK;
        }
    }
    return RTL8735B_BUS_ERROR;
}

int rtl8735b_spi_set_cs(uint8_t bus, uint32_t cs_pin, uint8_t asserted)
{
    gpio_t *gpio;
    if (bus != BUS_SPI_ID || cs_pin == 0U || _get_cs(cs_pin, &gpio) != RTL8735B_BUS_OK) {
        return RTL8735B_BUS_INVALID;
    }
    gpio_write(gpio, asserted ? 0 : 1);
    return RTL8735B_BUS_OK;
}

static int _spi_wait_idle(uint64_t deadline_us)
{
    uint64_t last_yield_us = rtl8735b_systime_us();
    for (;;) {
        __DMB();
        const uint8_t idle =
            !(spi_object.state & (SPI_STATE_RX_BUSY | SPI_STATE_TX_BUSY)) &&
            !spi_busy(&spi_object);
        const uint64_t now_us = rtl8735b_systime_us();
        if (idle) {
            // 先看狀態再看期限：bus task 晚醒時傳輸可能早已完成，資料有效，不算逾時。
            return RTL8735B_BUS_OK;
        }
        if (now_us >= deadline_us) {
            return RTL8735B_BUS_TIMEOUT;
        }
        if (now_us - last_yield_us >= 100ULL) {
            vTaskDelay(pdMS_TO_TICKS(1U));
            __DMB();
            last_yield_us = rtl8735b_systime_us();
        }
    }
}

static int _spi_execute(uint32_t len, uint64_t deadline_us)
{
    int result = _spi_wait_idle(deadline_us);
    if (result != RTL8735B_BUS_OK) {
        return result;
    }
    // 先確認空閒；SDK stream 內部還有無 timeout 的 bus-idle loop。
    const int32_t start_status = spi_master_write_read_stream(
        &spi_object, (char *)spi_tx_scratch, (char *)spi_rx_scratch, len);
    if (start_status == HAL_BUSY) {
        return RTL8735B_BUS_BUSY;
    }
    if (start_status != HAL_OK) {
        return RTL8735B_BUS_ERROR;
    }
    result = _spi_wait_idle(deadline_us);
    if (result == RTL8735B_BUS_TIMEOUT) {
        (void)hal_ssi_stop_recv(&spi_object.hal_ssi_adaptor);
        spi_disable(&spi_object);
        spi_faulted = 1U;
    }
    return result;
}

int rtl8735b_spi_transfer(uint8_t bus, uint32_t cs_pin,
                           const uint8_t *send, uint32_t send_len,
                           uint8_t *recv, uint32_t recv_len,
                           uint32_t timeout_ms, uint8_t keep_cs)
{
    gpio_t *gpio;
    if (bus != BUS_SPI_ID || !_valid_transfer(send, send_len, recv, recv_len) ||
        timeout_ms == 0U || timeout_ms > BUS_MAX_TIMEOUT_MS ||
        _get_cs(cs_pin, &gpio) != RTL8735B_BUS_OK) {
        return RTL8735B_BUS_INVALID;
    }
    if (!spi_initialized || spi_faulted) {
        return RTL8735B_BUS_ERROR;
    }

    const uint32_t total_len = send_len + recv_len;
    if (total_len > BUS_SPI_MAX_TRANSFER) {
        return RTL8735B_BUS_INVALID;
    }
    const uint64_t deadline_us = rtl8735b_systime_us() +
        (uint64_t)timeout_ms * 1000ULL;
    if (send_len > 0U) {
        memcpy(spi_tx_scratch, send, send_len);
    }
    if (recv_len > 0U) {
        memset(spi_tx_scratch + send_len, 0xFF, recv_len);
    }
    if (!keep_cs) {
        gpio_write(gpio, 0);
    }
    int result = _spi_execute(total_len, deadline_us);
    if (result == RTL8735B_BUS_OK && recv_len > 0U) {
        memcpy(recv, spi_rx_scratch + send_len, recv_len);
    }
    if (!keep_cs) {
        gpio_write(gpio, 1);
    }

    if (result == RTL8735B_BUS_TIMEOUT) {
        spi_faulted = 1U;
    }
    return result;
}

int rtl8735b_spi_transfer_fullduplex(uint8_t bus, uint32_t cs_pin,
                                     const uint8_t *send_recv,
                                     uint8_t *recv, uint32_t len,
                                     uint32_t timeout_ms, uint8_t keep_cs)
{
    gpio_t *gpio;
    if (bus != BUS_SPI_ID || send_recv == NULL || recv == NULL || len == 0U ||
        len > BUS_SPI_MAX_TRANSFER || timeout_ms == 0U || timeout_ms > BUS_MAX_TIMEOUT_MS ||
        _get_cs(cs_pin, &gpio) != RTL8735B_BUS_OK) {
        return RTL8735B_BUS_INVALID;
    }
    if (!spi_initialized || spi_faulted) {
        return RTL8735B_BUS_ERROR;
    }
    memcpy(spi_tx_scratch, send_recv, len);
    const uint64_t deadline_us = rtl8735b_systime_us() +
        (uint64_t)timeout_ms * 1000ULL;
    if (!keep_cs) {
        gpio_write(gpio, 0);
    }
    const int result = _spi_execute(len, deadline_us);
    if (result == RTL8735B_BUS_OK) {
        memcpy(recv, spi_rx_scratch, len);
    }
    if (!keep_cs) {
        gpio_write(gpio, 1);
    }
    if (result == RTL8735B_BUS_TIMEOUT) {
        spi_faulted = 1U;
    }
    return result;
}

int rtl8735b_spi_clock_pulse(uint8_t bus, uint32_t len,
                              uint32_t timeout_ms)
{
    if (bus != BUS_SPI_ID || !spi_initialized || spi_faulted || len == 0U ||
        len > BUS_SPI_MAX_TRANSFER || timeout_ms == 0U || timeout_ms > BUS_MAX_TIMEOUT_MS) {
        return RTL8735B_BUS_INVALID;
    }
    const uint64_t deadline_us = rtl8735b_systime_us() +
        (uint64_t)timeout_ms * 1000ULL;
    memset(spi_tx_scratch, 0xFF, len);
    const int result = _spi_execute(len, deadline_us);
    if (result == RTL8735B_BUS_TIMEOUT) {
        spi_faulted = 1U;
        spi_disable(&spi_object);
    }
    return result;
}
