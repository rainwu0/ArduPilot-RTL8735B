/* 主機測試用的 SDK 替身：目前開機的韌體槽（1 或 2）。 */
#pragma once
#include <stdint.h>
uint32_t hal_sys_get_ld_fw_idx(void);
