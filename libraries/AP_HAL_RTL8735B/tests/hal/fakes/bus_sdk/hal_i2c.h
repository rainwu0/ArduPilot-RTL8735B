#pragma once
// 測試替身。出處：名稱與數值依 SDK d1b6426 的 component/soc/8735b/fwlib/rtl8735b/include/hal_i2c.h（Apache-2.0）；只寫受測程式用到的部分，自己撰寫。
#include <stdint.h>

typedef enum { HAL_OK = 0, HAL_BUSY = 1, HAL_ERROR = 2 } hal_status_t;
enum {
    I2CStatusInitialized = 1, I2CStatusIdle = 2,
    I2CStatusTxReady = 3, I2CStatusTxing = 4,
    I2CStatusRxReady = 5, I2CStatusRxing = 6,
    I2CStatusError = 16, I2CStatusTimeOut = 17,
    I2CModeInterrupt = 1, I2CEnable = 1, I2CDisable = 0,
    I2CErrorNone = 0, I2CErrorTxAbort = 8, I2CErrorMasterAddrNack = 18,
};

typedef struct { uint32_t clock; } fake_i2c_init_dat_t;
typedef struct {
    uint16_t addr;
    uint8_t mst_stop;
    const uint8_t *buf;
    uint32_t len;
} fake_i2c_tx_t;
typedef struct {
    uint16_t addr;
    uint8_t mst_stop;
    uint8_t *buf;
    uint32_t len;
} fake_i2c_rx_t;
typedef struct {
    volatile uint8_t status;
    uint8_t op_mode;
    volatile uint32_t err_type;
    fake_i2c_init_dat_t init_dat;
    fake_i2c_tx_t tx_dat;
    fake_i2c_rx_t rx_dat;
} hal_i2c_adapter_t;

#ifdef __cplusplus
extern "C" {
#endif
hal_status_t hal_i2c_load_default(hal_i2c_adapter_t *, uint8_t index);
hal_status_t hal_i2c_init(hal_i2c_adapter_t *, uint32_t scl, uint32_t sda);
hal_status_t hal_i2c_deinit(hal_i2c_adapter_t *);
uint8_t hal_i2c_en_ctrl(hal_i2c_adapter_t *, uint8_t enable);
hal_status_t hal_i2c_set_clk(hal_i2c_adapter_t *);
hal_status_t hal_i2c_mst_restr_ctrl(hal_i2c_adapter_t *, uint8_t enable);
void hal_i2c_mst_restr_sw_ctrl(hal_i2c_adapter_t *, uint8_t enable);
hal_status_t hal_i2c_send(hal_i2c_adapter_t *);
hal_status_t hal_i2c_receive(hal_i2c_adapter_t *);
#ifdef __cplusplus
}
#endif
