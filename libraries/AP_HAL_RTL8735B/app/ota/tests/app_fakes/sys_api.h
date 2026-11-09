/* 主機測試用的 SDK 替身：重置（替身以 longjmp 離開，不回到呼叫端）。 */
#pragma once
void sys_reset(void);
