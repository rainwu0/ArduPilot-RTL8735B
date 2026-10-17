#include "UARTConfig.h"
#include <assert.h>
#include <stdio.h>

int main()
{
    const auto &console = RTL8735B_UART_CONFIGS[0];
    assert(console.uart_id == 1 && console.enabled);
    assert(console.tx_pin == 164 && console.rx_pin == 163);
    const auto &telem = RTL8735B_UART_CONFIGS[1];
    assert(telem.uart_id == 2 && telem.enabled);
    assert(telem.tx_pin == 111 && telem.rx_pin == 112);
    const auto &gps = RTL8735B_UART_CONFIGS[3];
    assert(gps.uart_id == 0 && gps.enabled);
    assert(gps.tx_pin == 2 && gps.rx_pin == 3);
    const auto &rc = RTL8735B_UART_CONFIGS[4];
    assert(rc.uart_id == 3 && rc.enabled);
    assert(rc.tx_pin == 129 && rc.rx_pin == 130);
    for (unsigned i = 0; i < 10; i++) {
        if (i != 0 && i != 1 && i != 3 && i != 4) {
            assert(!RTL8735B_UART_CONFIGS[i].enabled);
        }
    }
    puts("UART board mapping tests passed");
}
