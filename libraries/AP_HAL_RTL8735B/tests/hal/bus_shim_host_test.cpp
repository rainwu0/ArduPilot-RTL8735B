/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "bus_shim.h"
#include <diag.h>
#include <hal_i2c.h>
#include <hal_irq.h>
#include <hal_pinmux.h>
#include <hal_ssi.h>
#include <gpio_api.h>
#include <gpio_ex_api.h>
#include <objects.h>
#include <spi_api.h>
#include <spi_ex_api.h>
#include <task.h>

#include <cassert>
#include <cstdint>
#include <cstring>

namespace {
uint64_t mock_now_us;
uint8_t mock_nack_next_write;
uint8_t mock_i2c_timeout_next_write;
uint8_t mock_i2c_restart_enabled;
uint8_t mock_i2c_software_restart;
uint16_t mock_i2c_write_addr;
uint16_t mock_i2c_read_addr;
uint8_t mock_i2c_write_stop;
uint8_t mock_i2c_read_stop;
uint8_t mock_i2c_bus_num;
uint32_t mock_i2c_sda;
uint32_t mock_i2c_scl;
uint32_t mock_i2c_clock_hz;
uint32_t mock_spi_tx_len;
uint8_t mock_spi_tx[4096];
char *mock_spi_tx_ptr;
char *mock_spi_rx_ptr;
spi_t *mock_spi_object;
uint32_t mock_spi_delay_ticks;
uint32_t mock_delay_calls;
uint64_t mock_spi_complete_at;
uint8_t mock_spi_never_completes;
int mock_cs_levels[32];
uint32_t mock_cs_count;
uint8_t mock_abort_next_write;
// 傳送 leg 在下一次 vTaskDelay 才完成，且該次喚醒延遲這麼多微秒（模擬 bus task 被高優先 task 搶占）。
uint64_t mock_i2c_write_wake_late_us;
uint8_t mock_i2c_write_pending;
uint8_t mock_i2c_read_pending;
hal_i2c_adapter_t *mock_i2c_adapter;
uint8_t mock_abort_stuck;
uint32_t mock_i2c_init_calls;
uint32_t mock_i2c_deinit_calls;
// NVIC 的 I2C 中斷（這條匯流排的那一路）。替身的 hal_i2c_init 模擬 ROM 初始化時註冊並開啟中斷。
uint8_t mock_i2c_irq_enabled;
// adapter 剛被清零、hal_i2c_load_default 還沒填回時，中斷仍開著的次數：晚到的中斷會拿到清零的 adapter
uint32_t mock_i2c_zeroed_with_irq_enabled;
PinName mock_spi_ssel;
uint8_t mock_ssi_init_fail;
// SPI mode：SSI 啟用狀態，以及每次改 CPOL/CPHA 時 SSI 是否已停用
uint8_t mock_ssi_enabled;
uint8_t mock_ssi_polarity;
uint8_t mock_ssi_phase;
uint32_t mock_ssi_format_writes;
uint8_t mock_ssi_format_while_enabled;
uint8_t mock_ssi_cs_registered;
uint32_t mock_unregister_pin;
uint32_t mock_unregister_pid;
PinName mock_spi_sclk;
// I2C 逾時恢復：控制器停用、deinit、init 的順序，以及以 GPIO 清除匯流排。
uint32_t mock_op_order;
uint32_t mock_i2c_disable_order;
uint32_t mock_i2c_deinit_order;
uint32_t mock_i2c_init_order;
uint32_t mock_gpio_deinit_order;
hal_status_t mock_i2c_init_status = HAL_OK;
uint8_t mock_i2c_controller_enabled = 1U;
// SDA 被拉低：每個 SCL 脈衝減一，到 0 時放開（0xFF 表示一直拉低）。
uint8_t mock_sda_low_pulses;
uint32_t mock_scl_pulses;
uint32_t mock_i2c_send_calls;
uint8_t mock_gpio_owned[64];
uint8_t mock_scl_driven_low;
}

extern "C" {
uint32_t ConfigDebugErr = 0xFFFFFFFFU;
}

extern "C" uint64_t rtl8735b_systime_us(void)
{
    // 模擬獨立於 RTOS tick 前進的硬體微秒計時器，以及短交易的 IRQ。
    mock_now_us += 5U;
    if (mock_spi_complete_at != 0U && mock_now_us >= mock_spi_complete_at) {
        for (uint32_t i = 0; i < mock_spi_tx_len; i++) {
            mock_spi_rx_ptr[i] = static_cast<char>(mock_spi_tx[i] ^ 0xFFU);
        }
        mock_spi_object->state = 0U;
        mock_spi_complete_at = 0U;
    }
    return mock_now_us;
}

extern "C" void vTaskDelay(TickType_t ticks)
{
    mock_delay_calls++;
    mock_now_us += static_cast<uint64_t>(ticks) * 1000ULL;
    if (mock_i2c_write_pending) {
        mock_i2c_write_pending = 0U;
        mock_now_us += mock_i2c_write_wake_late_us;
        mock_i2c_adapter->tx_dat.len = 0;
        mock_i2c_adapter->status = I2CStatusIdle;
    } else if (mock_i2c_read_pending) {
        mock_i2c_read_pending = 0U;
        memset(mock_i2c_adapter->rx_dat.buf, 0xA5, mock_i2c_adapter->rx_dat.len);
        mock_i2c_adapter->rx_dat.len = 0;
        mock_i2c_adapter->status = I2CStatusIdle;
    }
    if (mock_spi_delay_ticks > 0U) {
        if (ticks >= mock_spi_delay_ticks) {
            mock_spi_delay_ticks = 0U;
            for (uint32_t i = 0; i < mock_spi_tx_len; i++) {
                mock_spi_rx_ptr[i] = static_cast<char>(mock_spi_tx[i] ^ 0xFFU);
            }
            mock_spi_object->state = 0U;
        } else {
            mock_spi_delay_ticks -= ticks;
        }
    }
}

extern "C" TaskHandle_t xTaskGetCurrentTaskHandle(void) { return nullptr; }

extern "C" void hal_irq_enable(int32_t irqn)
{
    assert(irqn == I2C0_IRQn + mock_i2c_bus_num);
    mock_i2c_irq_enabled = 1U;
}

extern "C" void hal_irq_disable(int32_t irqn)
{
    assert(irqn == I2C0_IRQn + mock_i2c_bus_num);
    mock_i2c_irq_enabled = 0U;
}

extern "C" void hal_irq_clear_pending(int32_t irqn)
{
    assert(irqn == I2C0_IRQn + mock_i2c_bus_num);
}

extern "C" hal_status_t hal_i2c_load_default(hal_i2c_adapter_t *adapter, uint8_t index)
{
    // _i2c_hw_init 在呼叫這裡之前把 adapter 清零
    if (mock_i2c_irq_enabled) {
        mock_i2c_zeroed_with_irq_enabled++;
    }
    mock_i2c_bus_num = index;
    adapter->status = I2CStatusInitialized;
    adapter->err_type = I2CErrorNone;
    return HAL_OK;
}

extern "C" hal_status_t hal_i2c_init(hal_i2c_adapter_t *adapter,
                                       uint32_t scl, uint32_t sda)
{
    mock_i2c_init_order = ++mock_op_order;
    // 腳位仍被 GPIO 持有時 I2C 註冊不到。
    assert(!mock_gpio_owned[scl] && !mock_gpio_owned[sda]);
    if (mock_i2c_init_status != HAL_OK) {
        return mock_i2c_init_status;
    }
    mock_i2c_controller_enabled = 1U;
    mock_i2c_scl = scl;
    mock_i2c_sda = sda;
    mock_i2c_init_calls++;
    mock_i2c_irq_enabled = 1U;
    // 重新初始化控制器後，前一次中止留下的狀態消失。
    mock_abort_stuck = 0U;
    adapter->status = I2CStatusIdle;
    return HAL_OK;
}

extern "C" hal_status_t hal_i2c_deinit(hal_i2c_adapter_t *)
{
    mock_i2c_deinit_calls++;
    mock_i2c_deinit_order = ++mock_op_order;
    return HAL_OK;
}

extern "C" uint8_t hal_i2c_en_ctrl(hal_i2c_adapter_t *, uint8_t enable)
{
    mock_i2c_controller_enabled = enable;
    if (!enable) {
        mock_i2c_disable_order = ++mock_op_order;
    }
    return 0U;
}

extern "C" hal_status_t hal_i2c_set_clk(hal_i2c_adapter_t *adapter)
{
    mock_i2c_clock_hz = adapter->init_dat.clock * 1000U;
    return HAL_OK;
}

extern "C" hal_status_t hal_i2c_mst_restr_ctrl(hal_i2c_adapter_t *, uint8_t enabled)
{
    mock_i2c_restart_enabled = enabled;
    return HAL_OK;
}

extern "C" void hal_i2c_mst_restr_sw_ctrl(hal_i2c_adapter_t *, uint8_t enabled)
{
    mock_i2c_software_restart = enabled;
}

extern "C" hal_status_t hal_i2c_send(hal_i2c_adapter_t *adapter)
{
    mock_i2c_send_calls++;
    mock_i2c_write_addr = adapter->tx_dat.addr;
    // SDK 契約：software restart 會覆蓋 mst_stop，不能只檢查欄位。
    mock_i2c_write_stop = mock_i2c_software_restart ? static_cast<uint8_t>(I2CDisable) : adapter->tx_dat.mst_stop;
    // 板上實測：位址中止後 SDK 把狀態設回 Idle、
    // err_type 為 TxAbort；控制器未重新初始化前，下一筆傳送停在 TxReady 不前進。
    if (mock_abort_stuck) {
        adapter->status = I2CStatusTxReady;
        return HAL_OK;
    }
    if (mock_abort_next_write) {
        mock_abort_next_write = 0U;
        mock_abort_stuck = 1U;
        adapter->err_type = I2CErrorTxAbort;
        adapter->status = I2CStatusIdle;
        return HAL_OK;
    }
    if (mock_i2c_timeout_next_write) {
        mock_i2c_timeout_next_write = 0;
        adapter->status = I2CStatusTxing;
        return HAL_OK;
    }
    if (mock_i2c_write_wake_late_us != 0U) {
        mock_i2c_adapter = adapter;
        mock_i2c_write_pending = 1U;
        adapter->status = I2CStatusTxing;
        return HAL_OK;
    }
    if (mock_nack_next_write) {
        mock_nack_next_write = 0;
        adapter->err_type = I2CErrorMasterAddrNack;
        adapter->status = I2CStatusError;
        return HAL_OK;
    }
    adapter->tx_dat.len = 0;
    adapter->status = I2CStatusIdle;
    return HAL_OK;
}

extern "C" hal_status_t hal_i2c_receive(hal_i2c_adapter_t *adapter)
{
    mock_i2c_read_addr = adapter->rx_dat.addr;
    mock_i2c_read_stop = mock_i2c_software_restart ? static_cast<uint8_t>(I2CDisable) : adapter->rx_dat.mst_stop;
    if (mock_i2c_write_wake_late_us != 0U) {
        // 接收 leg 照常在下一個 tick 內完成。
        mock_i2c_adapter = adapter;
        mock_i2c_read_pending = 1U;
        adapter->status = I2CStatusRxing;
        return HAL_OK;
    }
    memset(adapter->rx_dat.buf, 0xA5, adapter->rx_dat.len);
    adapter->rx_dat.len = 0;
    adapter->status = I2CStatusIdle;
    return HAL_OK;
}

extern "C" hal_status_t hal_ssi_set_sclk(phal_ssi_adaptor_t *, uint32_t)
{
    return HAL_OK;
}

extern "C" hal_status_t hal_ssi_stop_recv(phal_ssi_adaptor_t *) { return HAL_OK; }
extern "C" hal_status_t hal_ssi_enable(phal_ssi_adaptor_t *) { mock_ssi_enabled = 1U; return HAL_OK; }
extern "C" hal_status_t hal_ssi_disable(phal_ssi_adaptor_t *) { mock_ssi_enabled = 0U; return HAL_OK; }
extern "C" hal_status_t hal_ssi_set_sclk_polarity(phal_ssi_adaptor_t *, uint8_t polarity)
{
    mock_ssi_format_writes++;
    mock_ssi_format_while_enabled |= mock_ssi_enabled;
    mock_ssi_polarity = polarity;
    return HAL_OK;
}
extern "C" hal_status_t hal_ssi_set_sclk_phase(phal_ssi_adaptor_t *, uint8_t phase)
{
    mock_ssi_format_writes++;
    mock_ssi_format_while_enabled |= mock_ssi_enabled;
    mock_ssi_phase = phase;
    return HAL_OK;
}

bool is_i2c_pin(uint32_t pin) { return pin == PIN_F1 || pin == PIN_F2; }

extern "C" void gpio_init(gpio_t *gpio, int pin)
{
    gpio->pin = pin;
    if (pin >= 0 && pin < 64) {
        mock_gpio_owned[pin] = 1U;
    }
}
extern "C" void gpio_deinit(gpio_t *gpio)
{
    mock_gpio_owned[gpio->pin] = 0U;
    mock_gpio_deinit_order = ++mock_op_order;
}
// SCL 以「輸出 0」拉低、改回輸入放開（開汲極）；每次放開算一個脈衝。
extern "C" void gpio_dir(gpio_t *gpio, PinDirection dir)
{
    if (gpio->pin != PIN_F1) {
        return;
    }
    if (dir == PIN_OUTPUT) {
        mock_scl_driven_low = 1U;
    } else if (mock_scl_driven_low) {
        mock_scl_driven_low = 0U;
        mock_scl_pulses++;
        if (mock_sda_low_pulses != 0U && mock_sda_low_pulses != 0xFFU) {
            mock_sda_low_pulses--;
        }
    }
}
extern "C" void gpio_write(gpio_t *gpio, int value)
{
    if (is_i2c_pin(gpio->pin)) {
        // 開汲極模擬只能輸出 0。
        assert(value == 0);
        return;
    }
    if (mock_cs_count < 32U) {
        mock_cs_levels[mock_cs_count++] = value;
    }
}
// 只讀 SDA：PF2 可以模擬被拉住，其他情境的 SDA（PE6、PA1）一律放開。
extern "C" int gpio_read(gpio_t *gpio)
{
    assert(gpio->pin == PIN_F2 || gpio->pin == PIN_E6 || gpio->pin == PIN_A1);
    if (gpio->pin != PIN_F2) {
        return 1;
    }
    return mock_sda_low_pulses == 0U ? 1 : 0;
}

extern "C" void spi_init(spi_t *spi, PinName mosi, PinName miso,
                           PinName sclk, PinName ssel)
{
    mock_spi_object = spi;
    (void)spi;
    (void)mosi;
    (void)miso;
    mock_spi_sclk = sclk;
    mock_spi_ssel = ssel;
}
extern "C" void spi_free(spi_t *) { mock_ssi_cs_registered = 0U; }
extern "C" void spi_format(spi_t *spi, int, int, int)
{
    // SDK：spi_format 先依 SCLK 設 index，再由 hal_ssi_init → hal_ssi_pin_ctl 以 PID_SPIx
    // 註冊 CS/CLK/MISO/MOSI 四腳；任一腳（例如 NC）註冊失敗就只印錯誤並返回，不回報狀態。
    // 出處：Ameba-AIoT/ameba-rtos-pro2 fwlib/rtl8735b/source/ram/hal_ssi.c:61-121。
    // SCLK 在 PE1 的是 SPI0，其餘（PF6）是 SPI1。
    spi->hal_ssi_adaptor.index = mock_spi_sclk == PIN_E1 ? 0U : 1U;
    mock_ssi_cs_registered = !mock_ssi_init_fail && mock_spi_ssel != NC;
}
extern "C" hal_status_t hal_pinmux_unregister(uint32_t pin_name, uint32_t periphl_id)
{
    mock_unregister_pin = pin_name;
    mock_unregister_pid = periphl_id;
    // SDK hal_pinmux.h：週邊不擁有該腳時回傳錯誤。登記這些腳的是 spi_format 選到的那組 SPI。
    const uint32_t owner = mock_spi_sclk == PIN_E1 ? PID_SPI0 : PID_SPI1;
    if (!mock_ssi_cs_registered || pin_name != static_cast<uint32_t>(mock_spi_ssel) ||
        periphl_id != owner) {
        return HAL_ERROR;
    }
    mock_ssi_cs_registered = 0U;
    return HAL_OK;
}
extern "C" void spi_enable(spi_t *) { mock_ssi_enabled = 1U; }
extern "C" void spi_disable(spi_t *spi) { (void)spi; }
extern "C" int spi_busy(spi_t *spi)
{
    return mock_spi_never_completes ||
        ((spi->state & (SPI_STATE_RX_BUSY | SPI_STATE_TX_BUSY)) != 0U);
}
extern "C" int32_t spi_master_write_read_stream(spi_t *spi, char *tx,
                                                  char *rx, uint32_t len)
{
    mock_spi_tx_len = len;
    mock_spi_tx_ptr = tx;
    mock_spi_rx_ptr = rx;
    memcpy(mock_spi_tx, tx, len);
    if (!mock_spi_never_completes && mock_spi_delay_ticks == 0U && mock_spi_complete_at == 0U) {
        for (uint32_t i = 0; i < len; i++) {
            rx[i] = static_cast<char>(static_cast<uint8_t>(tx[i]) ^ 0xFFU);
        }
        spi->state = 0U;
    } else {
        spi->state = SPI_STATE_RX_BUSY | SPI_STATE_TX_BUSY;
    }
    return HAL_OK;
}

// 逾時後重新初始化失敗：立即封鎖，之後不再碰控制器。
static int reinit_failure_scenario()
{
    assert(rtl8735b_i2c_init(1U, 1U, PIN_F2, PIN_F1, 400000U) == RTL8735B_BUS_OK);
    const uint8_t reg = 0xD0U;
    mock_i2c_timeout_next_write = 1U;
    mock_i2c_init_status = HAL_ERROR;
    assert(rtl8735b_i2c_transfer(1U, 0x76U, &reg, 1U, nullptr, 0U, 0U, 1U) ==
           RTL8735B_BUS_TIMEOUT);
    mock_i2c_init_status = HAL_OK;
    const uint32_t sends = mock_i2c_send_calls;
    assert(rtl8735b_i2c_transfer(1U, 0x76U, &reg, 1U, nullptr, 0U, 0U, 4U) ==
           RTL8735B_BUS_TIMEOUT);
    assert(mock_i2c_send_calls == sends);
    return 0;
}

// 邏輯匯流排與 SDK 編號分開：邏輯匯流排 3 接 SDK 的 I2C2，邏輯匯流排 2 接 SDK 的 SPI0。
static int alt_index_scenario()
{
    const uint8_t reg = 0xD0U;
    uint8_t value = 0U;
    assert(rtl8735b_i2c_init(1U, 4U, PIN_E6, PIN_E5, 400000U) == RTL8735B_BUS_INVALID);
    assert(rtl8735b_i2c_init(3U, 2U, PIN_E6, PIN_E5, 100000U) == RTL8735B_BUS_OK);
    assert(mock_i2c_bus_num == 2U);
    assert(mock_i2c_sda == PIN_E6 && mock_i2c_scl == PIN_E5 && mock_i2c_clock_hz == 100000U);
    assert(rtl8735b_i2c_transfer(3U, 0x76U, &reg, 1U, &value, 1U, 0U, 4U) == RTL8735B_BUS_OK);
    assert(value == 0xA5U);
    assert(rtl8735b_i2c_transfer(1U, 0x76U, &reg, 1U, &value, 1U, 0U, 4U) == RTL8735B_BUS_INVALID);

    // SDK 給的 SSI 編號和要求的不同（SCLK 在 PF6 是 SPI1）：拒絕
    assert(rtl8735b_spi_init(2U, 0U, PIN_F7, PIN_F5, PIN_F6, PIN_F8, 1000000U, 0U) == RTL8735B_BUS_ERROR);
    assert(rtl8735b_spi_init(2U, 2U, PIN_E3, PIN_E2, PIN_E1, PIN_E4, 1000000U, 0U) == RTL8735B_BUS_INVALID);
    assert(rtl8735b_spi_init(2U, 0U, PIN_E3, PIN_E2, PIN_E1, PIN_E4, 1000000U, 0U) == RTL8735B_BUS_OK);
    assert(mock_unregister_pin == PIN_E4 && mock_unregister_pid == PID_SPI0);
    assert(rtl8735b_spi_set_cs(2U, 88U, 0U) == RTL8735B_BUS_OK);
    assert(rtl8735b_spi_set_cs(1U, 88U, 0U) == RTL8735B_BUS_INVALID);
    return 0;
}

// 開機時 SDA 已被拉住：重置沒有讓感測器斷電時，裝置可能停在送資料位元的中途。初始化控制器之前
// 先以 GPIO 清除匯流排，GPIO 釋放腳位後才交給 I2C；這時控制器還沒初始化，不需要 deinit。
static int init_clear_scenario()
{
    mock_sda_low_pulses = 3U;
    assert(rtl8735b_i2c_init(1U, 1U, PIN_F2, PIN_F1, 400000U) == RTL8735B_BUS_OK);
    assert(mock_scl_pulses == 3U);
    assert(!mock_gpio_owned[PIN_F1] && !mock_gpio_owned[PIN_F2]);
    assert(mock_gpio_deinit_order != 0U && mock_gpio_deinit_order < mock_i2c_init_order);
    assert(mock_i2c_deinit_calls == 0U);
    const uint8_t reg = 0xD0U;
    uint8_t value = 0U;
    assert(rtl8735b_i2c_transfer(1U, 0x76U, &reg, 1U, &value, 1U, 0U, 4U) == RTL8735B_BUS_OK);
    assert(value == 0xA5U);
    return 0;
}

// 腳位值 0 是合法的腳位：SDK 的 PIN_A0 就是 0，是 I2C0 的 SCL（SDA 是 PIN_A1）。
// 「沒接」要用 SDK 的 NC，不能拿 0 當作沒設定。SPI 的片選可以是任何 GPIO，同樣要接受 0、拒絕 NC。
// shim 只允許初始化一次 I2C，所以獨立成一個情境。
static int pin_zero_scenario()
{
    // NC 當 SDA 或 SCL 都是沒接，拒絕；被拒絕不能占用這唯一一次的初始化。
    assert(rtl8735b_i2c_init(1U, 0U, static_cast<uint32_t>(NC), PIN_A0, 400000U) == RTL8735B_BUS_INVALID);
    assert(rtl8735b_i2c_init(1U, 0U, PIN_A1, static_cast<uint32_t>(NC), 400000U) == RTL8735B_BUS_INVALID);
    assert(mock_i2c_init_calls == 0U);

    // I2C0：SDA = PA1、SCL = PA0。SDK 收到的匯流排編號與兩支腳位都要正確。
    assert(rtl8735b_i2c_init(1U, 0U, PIN_A1, PIN_A0, 400000U) == RTL8735B_BUS_OK);
    assert(mock_i2c_init_calls == 1U);
    assert(mock_i2c_bus_num == 0U);
    assert(mock_i2c_sda == PIN_A1 && mock_i2c_scl == PIN_A0);
    assert(mock_i2c_clock_hz == 400000U);
    // SDA 沒被拉住，清除匯流排不送脈衝，腳位也不留在 GPIO 手上。
    assert(mock_scl_pulses == 0U);
    assert(!mock_gpio_owned[PIN_A0] && !mock_gpio_owned[PIN_A1]);
    const uint8_t reg = 0xD0U;
    uint8_t value = 0U;
    assert(rtl8735b_i2c_transfer(1U, 0x76U, &reg, 1U, &value, 1U, 0U, 4U) == RTL8735B_BUS_OK);
    assert(value == 0xA5U);
    assert(mock_i2c_write_addr == 0x76U && mock_i2c_read_addr == 0x76U);

    // SPI：SSI 用的腳位照常（SPI1），片選另外以 GPIO 軟體控制。
    assert(rtl8735b_spi_init(1U, 1U, PIN_F7, PIN_F5, PIN_F6, PIN_F8, 1000000U, 0U) == RTL8735B_BUS_OK);
    // 片選是 PIN_A0：拉低與放開都成功，而且真的操作到那一腳。
    assert(rtl8735b_spi_set_cs(1U, PIN_A0, 0U) == RTL8735B_BUS_OK);
    mock_cs_count = 0U;
    assert(rtl8735b_spi_set_cs(1U, PIN_A0, 1U) == RTL8735B_BUS_OK);
    assert(mock_cs_count == 1U && mock_cs_levels[0] == 0);
    assert(rtl8735b_spi_set_cs(1U, PIN_A0, 0U) == RTL8735B_BUS_OK);
    assert(mock_cs_count == 2U && mock_cs_levels[1] == 1);
    // 一般傳輸也能以 PIN_A0 當片選：傳輸期間拉低、結束放開。
    mock_cs_count = 0U;
    const uint8_t tx[] = {0x12U, 0x34U};
    uint8_t rx[2] = {};
    assert(rtl8735b_spi_transfer(1U, PIN_A0, tx, 2U, rx, 2U, 20U, 0U) == RTL8735B_BUS_OK);
    assert(mock_cs_count == 2U && mock_cs_levels[0] == 0 && mock_cs_levels[1] == 1);
    uint8_t duplex[2] = {0x01U, 0xFEU};
    mock_cs_count = 0U;
    assert(rtl8735b_spi_transfer_fullduplex(1U, PIN_A0, duplex, duplex, 2U, 20U, 0U) == RTL8735B_BUS_OK);
    assert(mock_cs_count == 2U && mock_cs_levels[0] == 0 && mock_cs_levels[1] == 1);
    assert(duplex[0] == 0xFEU && duplex[1] == 0x01U);

    // 片選是 NC（沒接）：拒絕，而且不碰任何腳位。
    mock_cs_count = 0U;
    assert(rtl8735b_spi_set_cs(1U, static_cast<uint32_t>(NC), 1U) == RTL8735B_BUS_INVALID);
    assert(mock_cs_count == 0U);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "alt-index") == 0) {
        return alt_index_scenario();
    }
    if (argc > 1 && strcmp(argv[1], "reinit-failure") == 0) {
        return reinit_failure_scenario();
    }
    if (argc > 1 && strcmp(argv[1], "init-clear") == 0) {
        return init_clear_scenario();
    }
    if (argc > 1 && strcmp(argv[1], "pin-zero") == 0) {
        return pin_zero_scenario();
    }
    assert(rtl8735b_i2c_init(1U, 1U, PIN_F2, PIN_F1, 400000U) == RTL8735B_BUS_OK);
    // SDA 沒被拉住時，初始化不在 SCL 送脈衝，也不留著 GPIO。
    assert(mock_scl_pulses == 0U);
    assert(!mock_gpio_owned[PIN_F1] && !mock_gpio_owned[PIN_F2]);
    // SDK 在位址 NACK 時於 LOG UART 同步列印約 128 字元（115200 baud 約 10 ms）；
    // HAL 以回傳碼報告失敗，因此只關閉 SDK 的 I2C 錯誤列印，其他模組不變。
    assert((ConfigDebugErr & _DBG_I2C_) == 0U);
    assert(ConfigDebugErr == (0xFFFFFFFFU & ~_DBG_I2C_));
    assert(mock_i2c_bus_num == 1U);
    assert(mock_i2c_sda == PIN_F2 && mock_i2c_scl == PIN_F1);
    assert(mock_i2c_clock_hz == 400000U && mock_i2c_restart_enabled == 1U);
    assert(mock_i2c_software_restart == I2CDisable);

    const uint8_t reg = 0xD0U;
    uint8_t value = 0U;
    assert(rtl8735b_i2c_transfer(1U, 0x76U, &reg, 1U, &value, 1U, 0U, 4U) ==
           RTL8735B_BUS_OK);
    assert(mock_i2c_write_addr == 0x76U && mock_i2c_read_addr == 0x76U);
    assert(mock_i2c_write_stop == I2CDisable && mock_i2c_read_stop == I2CEnable);
    assert(value == 0xA5U);
    assert(mock_i2c_software_restart == I2CDisable);

    assert(rtl8735b_i2c_transfer(1U, 0x1EU, &reg, 1U, &value, 1U, 1U, 4U) ==
           RTL8735B_BUS_OK);
    assert(mock_i2c_write_addr == 0x1EU && mock_i2c_read_addr == 0x1EU);
    assert(mock_i2c_write_stop == I2CEnable && mock_i2c_read_stop == I2CEnable);
    assert(rtl8735b_i2c_transfer(1U, 0x76U, nullptr, 0U, &value, 1U, 0U, 4U) ==
           RTL8735B_BUS_OK);
    assert(mock_i2c_read_stop == I2CEnable);

    mock_nack_next_write = 1U;
    assert(rtl8735b_i2c_transfer(1U, 0x76U, &reg, 1U, &value, 1U, 0U, 4U) ==
           RTL8735B_BUS_ERROR);
    assert(mock_i2c_software_restart == I2CDisable);
    assert(rtl8735b_i2c_transfer(1U, 0x1EU, &reg, 1U, nullptr, 0U, 0U, 4U) ==
           RTL8735B_BUS_OK);
    assert(mock_i2c_write_stop == I2CEnable);

    // SSI 需要可註冊的 CS 腳；沒有接（NC）就不能初始化。
    assert(rtl8735b_spi_init(1U, 1U, PIN_F7, PIN_F5, PIN_F6, (uint32_t)NC, 1000000U, 0U) ==
           RTL8735B_BUS_INVALID);
    // SDK 的 spi_format 不回報 hal_ssi_init 失敗；shim 必須自己偵測並回報。
    mock_ssi_init_fail = 1U;
    assert(rtl8735b_spi_init(1U, 1U, PIN_F7, PIN_F5, PIN_F6, PIN_F8, 1000000U, 0U) ==
           RTL8735B_BUS_ERROR);
    mock_ssi_init_fail = 0U;
    assert(rtl8735b_spi_init(1U, 1U, PIN_F7, PIN_F5, PIN_F6, PIN_F8, 1000000U, 0U) ==
           RTL8735B_BUS_OK);
    // CS 腳交給 SSI 完成註冊後，立即從 SPI1 解除，改由 GPIO 軟體片選。
    assert(mock_spi_ssel == PIN_F8);
    assert(mock_unregister_pin == PIN_F8 && mock_unregister_pid == PID_SPI1);
    assert(mock_ssi_cs_registered == 0U);
    assert(rtl8735b_spi_set_cs(1U, 88U, 0U) == RTL8735B_BUS_OK);
    mock_cs_count = 0U;
    const uint8_t tx[] = {0x12U, 0x34U};
    uint8_t rx[2] = {};
    assert(rtl8735b_spi_transfer(1U, 88U, tx, 2U, rx, 2U, 20U, 0U) ==
           RTL8735B_BUS_OK);
    assert(mock_spi_tx_len == 4U);
    assert(mock_spi_tx[0] == 0x12U && mock_spi_tx[1] == 0x34U);
    assert(mock_spi_tx[2] == 0xFFU && mock_spi_tx[3] == 0xFFU);
    assert(rx[0] == 0x00U && rx[1] == 0x00U);
    assert(mock_cs_count == 2U && mock_cs_levels[0] == 0 && mock_cs_levels[1] == 1);

    const uint8_t full_duplex_tx[] = {0x01U, 0xFEU, 0x55U};
    uint8_t full_duplex_rx[3] = {};
    assert(rtl8735b_spi_transfer_fullduplex(1U, 88U, full_duplex_tx,
           full_duplex_rx, 3U, 20U, 0U) == RTL8735B_BUS_OK);
    assert(mock_spi_tx_len == 3U && mock_spi_tx[0] == 0x01U &&
           mock_spi_tx[1] == 0xFEU && mock_spi_tx[2] == 0x55U);
    assert(full_duplex_rx[0] == 0xFEU && full_duplex_rx[1] == 0x01U &&
           full_duplex_rx[2] == 0xAAU);

    // 每個裝置自己的 SPI mode：初始化時是 mode 0，同 mode 不重寫；改 mode 時先停用 SSI 再設 CPOL/CPHA，
    // 設完重新啟用（推論：SSI 啟用中不能改格式，SDK 文件沒寫，需上板確認）。
    assert(rtl8735b_spi_set_mode(1U, 0U) == RTL8735B_BUS_OK && mock_ssi_format_writes == 0U);
    assert(rtl8735b_spi_set_mode(1U, 3U) == RTL8735B_BUS_OK);
    assert(mock_ssi_polarity == ScpolInactiveIsHigh && mock_ssi_phase == ScphToggleAtStart);
    assert(!mock_ssi_format_while_enabled && mock_ssi_enabled);
    assert(rtl8735b_spi_set_mode(1U, 2U) == RTL8735B_BUS_OK);
    assert(mock_ssi_polarity == ScpolInactiveIsHigh && mock_ssi_phase == ScphToggleInMiddle);
    assert(rtl8735b_spi_set_mode(1U, 1U) == RTL8735B_BUS_OK);
    assert(mock_ssi_polarity == ScpolInactiveIsLow && mock_ssi_phase == ScphToggleAtStart);
    assert(rtl8735b_spi_set_mode(1U, 4U) == RTL8735B_BUS_INVALID);
    assert(rtl8735b_spi_set_mode(2U, 0U) == RTL8735B_BUS_INVALID);
    assert(rtl8735b_spi_set_mode(1U, 0U) == RTL8735B_BUS_OK);
    assert(mock_ssi_polarity == ScpolInactiveIsLow && mock_ssi_phase == ScphToggleInMiddle);

    uint8_t alias[] = {0x91U, 0x2EU};
    assert(rtl8735b_spi_transfer_fullduplex(1U, 88U, alias, alias, 2U,
           20U, 0U) == RTL8735B_BUS_OK);
    assert(alias[0] == 0x6EU && alias[1] == 0xD1U);

    uint8_t fifo_tx[1024];
    uint8_t fifo_rx[1024] = {};
    for (uint32_t i = 0; i < sizeof(fifo_tx); i++) {
        fifo_tx[i] = static_cast<uint8_t>(i * 13U);
    }
    assert(rtl8735b_spi_transfer_fullduplex(1U, 88U, fifo_tx, fifo_rx,
           sizeof(fifo_tx), 20U, 0U) == RTL8735B_BUS_OK);
    assert(mock_spi_tx_len == sizeof(fifo_tx));
    assert(fifo_rx[0] == static_cast<uint8_t>(fifo_tx[0] ^ 0xFFU));
    assert(fifo_rx[1023] == static_cast<uint8_t>(fifo_tx[1023] ^ 0xFFU));

    const uint32_t before_short_delays = mock_delay_calls;
    mock_spi_complete_at = mock_now_us + 70U;
    assert(rtl8735b_spi_transfer_fullduplex(1U, 88U, full_duplex_tx,
           full_duplex_rx, 3U, 20U, 0U) == RTL8735B_BUS_OK);
    assert(mock_delay_calls == before_short_delays);
    assert(full_duplex_rx[0] == 0xFEU && full_duplex_rx[1] == 0x01U);

    mock_spi_delay_ticks = 2U;
    const uint64_t before_delayed_transfer = mock_now_us;
    assert(rtl8735b_spi_transfer_fullduplex(1U, 88U, full_duplex_tx,
           full_duplex_rx, 3U, 20U, 0U) == RTL8735B_BUS_OK);
    assert(mock_now_us - before_delayed_transfer >= 2000U);
    assert(mock_delay_calls >= before_short_delays + 2U);
    assert(full_duplex_rx[0] == 0xFEU && full_duplex_rx[1] == 0x01U);

    // 傳輸已完成、bus task 晚醒才看到：資料有效，不能回報逾時而封鎖 SPI（IMU 所在匯流排）。
    mock_spi_delay_ticks = 1U;
    assert(rtl8735b_spi_transfer_fullduplex(1U, 88U, full_duplex_tx,
           full_duplex_rx, 3U, 1U, 0U) == RTL8735B_BUS_OK);
    assert(full_duplex_rx[0] == 0xFEU && full_duplex_rx[1] == 0x01U);

    mock_cs_count = 0U;
    assert(rtl8735b_spi_clock_pulse(1U, 2U, 20U) == RTL8735B_BUS_OK);
    assert(mock_cs_count == 0U);

    mock_spi_never_completes = 1U;
    assert(rtl8735b_spi_transfer_fullduplex(1U, 88U, full_duplex_tx,
           full_duplex_rx, 3U, 1U, 0U) == RTL8735B_BUS_TIMEOUT);
    assert(mock_spi_tx_ptr != nullptr && mock_spi_tx_ptr[0] == 0x01);
    // 模擬 timeout 後才到達的 IRQ；scratch 仍有效，而 bus 已不可重用。
    mock_spi_object->state = 0U;
    assert(mock_spi_tx_ptr[0] == 0x01);
    assert(rtl8735b_spi_transfer_fullduplex(1U, 88U, full_duplex_tx,
           full_duplex_rx, 3U, 1U, 0U) == RTL8735B_BUS_ERROR);

    // 位址中止（探測不存在的裝置）必須回報 ERROR 並恢復控制器，不能讓同匯流排的其他裝置失效。
    const uint32_t inits_before_abort = mock_i2c_init_calls;
    const uint32_t deinits_before_abort = mock_i2c_deinit_calls;
    mock_abort_next_write = 1U;
    assert(rtl8735b_i2c_transfer(1U, 0x1EU, &reg, 1U, &value, 1U, 0U, 4U) ==
           RTL8735B_BUS_ERROR);
    assert(mock_i2c_deinit_calls == deinits_before_abort + 1U);
    assert(mock_i2c_init_calls == inits_before_abort + 1U);
    // 重新初始化期間 I2C 中斷是關的，晚到的中斷不會拿到清零的 adapter；完成後重新開啟
    assert(mock_i2c_zeroed_with_irq_enabled == 0U);
    assert(mock_i2c_irq_enabled);
    assert(mock_i2c_sda == PIN_F2 && mock_i2c_scl == PIN_F1);
    assert(mock_i2c_clock_hz == 400000U && mock_i2c_restart_enabled == 1U);
    value = 0U;
    assert(rtl8735b_i2c_transfer(1U, 0x76U, &reg, 1U, &value, 1U, 0U, 4U) ==
           RTL8735B_BUS_OK);
    assert(value == 0xA5U);

    // 板上實測：Wi-Fi 初始化期間，Copter 開機讀 BMP280 ID 的
    // 寫後讀交易耗時約 4–5 ms。bus task 在傳送 leg 完成後晚醒、接收 leg 在原 timeout 之後才開始，
    // 而接收 leg 本身只需不到一個 tick；這不是匯流排逾時，必須回報 OK 並保持控制器可用。
    mock_i2c_write_wake_late_us = 4000U;
    value = 0U;
    assert(rtl8735b_i2c_transfer(1U, 0x76U, &reg, 1U, &value, 1U, 0U, 4U) ==
           RTL8735B_BUS_OK);
    assert(value == 0xA5U);
    mock_i2c_write_wake_late_us = 0U;
    value = 0U;
    assert(rtl8735b_i2c_transfer(1U, 0x76U, &reg, 1U, &value, 1U, 0U, 4U) ==
           RTL8735B_BUS_OK);
    assert(value == 0xA5U);

    // 逾時：先停用控制器，再 deinit、重新初始化；這筆回報 TIMEOUT，匯流排之後照常可用
    // （氣壓計與外接羅盤共用，一次逾時不能讓整條匯流排失效到重開）。SDA 沒被拉住時不送脈衝。
    mock_i2c_timeout_next_write = 1U;
    const uint32_t inits_before_timeout = mock_i2c_init_calls;
    assert(rtl8735b_i2c_transfer(1U, 0x76U, &reg, 1U, nullptr, 0U, 0U, 1U) ==
           RTL8735B_BUS_TIMEOUT);
    assert(mock_i2c_init_calls == inits_before_timeout + 1U);
    assert(mock_i2c_zeroed_with_irq_enabled == 0U);
    assert(mock_i2c_irq_enabled);
    assert(mock_i2c_disable_order != 0U && mock_i2c_disable_order < mock_i2c_deinit_order);
    assert(mock_i2c_deinit_order < mock_i2c_init_order);
    assert(mock_scl_pulses == 0U);
    assert(mock_i2c_sda == PIN_F2 && mock_i2c_scl == PIN_F1);
    assert(mock_i2c_clock_hz == 400000U && mock_i2c_restart_enabled == 1U);
    value = 0U;
    assert(rtl8735b_i2c_transfer(1U, 0x76U, &reg, 1U, &value, 1U, 0U, 4U) ==
           RTL8735B_BUS_OK);
    assert(value == 0xA5U);

    // SDA 被裝置拉住：以 GPIO 在 SCL 送脈衝，SDA 放開就停；GPIO 先釋放腳位再交回 I2C。
    mock_sda_low_pulses = 3U;
    mock_i2c_timeout_next_write = 1U;
    assert(rtl8735b_i2c_transfer(1U, 0x76U, &reg, 1U, nullptr, 0U, 0U, 1U) ==
           RTL8735B_BUS_TIMEOUT);
    assert(mock_scl_pulses == 3U);
    assert(!mock_gpio_owned[PIN_F1] && !mock_gpio_owned[PIN_F2]);
    assert(mock_i2c_deinit_order < mock_gpio_deinit_order);
    assert(mock_gpio_deinit_order < mock_i2c_init_order);
    // 成功的交易把連續逾時次數歸零。
    assert(rtl8735b_i2c_transfer(1U, 0x76U, &reg, 1U, &value, 1U, 0U, 4U) ==
           RTL8735B_BUS_OK);

    // 一直拉住：最多 9 個脈衝，照樣重新初始化。
    mock_scl_pulses = 0U;
    mock_sda_low_pulses = 0xFFU;
    mock_i2c_timeout_next_write = 1U;
    assert(rtl8735b_i2c_transfer(1U, 0x76U, &reg, 1U, nullptr, 0U, 0U, 1U) ==
           RTL8735B_BUS_TIMEOUT);
    assert(mock_scl_pulses == 9U);
    mock_sda_low_pulses = 0U;
    assert(!mock_gpio_owned[PIN_F1] && !mock_gpio_owned[PIN_F2]);

    // 連續第二次逾時仍恢復；連續第三次才封鎖，之後不再碰控制器。
    mock_i2c_timeout_next_write = 1U;
    assert(rtl8735b_i2c_transfer(1U, 0x76U, &reg, 1U, nullptr, 0U, 0U, 1U) ==
           RTL8735B_BUS_TIMEOUT);
    const uint32_t inits_before_third = mock_i2c_init_calls;
    mock_i2c_timeout_next_write = 1U;
    assert(rtl8735b_i2c_transfer(1U, 0x76U, &reg, 1U, nullptr, 0U, 0U, 1U) ==
           RTL8735B_BUS_TIMEOUT);
    assert(mock_i2c_init_calls == inits_before_third);
    const uint32_t sends_after_fault = mock_i2c_send_calls;
    assert(rtl8735b_i2c_transfer(1U, 0x1EU, &reg, 1U, nullptr, 0U, 0U, 4U) ==
           RTL8735B_BUS_TIMEOUT);
    assert(mock_i2c_send_calls == sends_after_fault);
}
