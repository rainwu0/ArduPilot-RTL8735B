#pragma once
// 測試替身。名稱照 ArduPilot 的 GCS_MAVLink/GCS.h；訊息印到標準輸出。
#include <stdio.h>
#define MAV_SEVERITY_NOTICE 5
#define GCS_SEND_TEXT(severity, format, ...) do { (void)(severity); printf(format "\n", ##__VA_ARGS__); } while (0)
