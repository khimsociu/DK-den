/**
 * @file dtg_reset_reason_map.h
 * @brief Map ESP8266 rst_info.reason onto Hub tele/snap `rst` values (ESP32 enum numbers).
 *
 * Host-testable (no Arduino). Keep numeric values aligned with esp_reset_reason_t.
 */
#pragma once

#include <stdint.h>

enum : uint8_t {
  DTG_RST_UNKNOWN = 0,
  DTG_RST_POWERON = 1,
  DTG_RST_EXT = 2,
  DTG_RST_SW = 3,
  DTG_RST_PANIC = 4,
  DTG_RST_INT_WDT = 5,
  DTG_RST_TASK_WDT = 6,
  DTG_RST_WDT = 7,
  DTG_RST_DEEPSLEEP = 8,
  DTG_RST_BROWNOUT = 9
};

/** ESP8266 rst_reason -> Hub rst (ESP32-compatible). */
constexpr uint8_t dtgMapEsp8266ResetReason(uint8_t reason)
{
  switch (reason)
  {
  case 0: // REASON_DEFAULT_RST
    return DTG_RST_POWERON;
  case 1: // REASON_WDT_RST
    return DTG_RST_WDT;
  case 2: // REASON_EXCEPTION_RST
    return DTG_RST_PANIC;
  case 3: // REASON_SOFT_WDT_RST
    return DTG_RST_TASK_WDT;
  case 4: // REASON_SOFT_RESTART
    return DTG_RST_SW;
  case 5: // REASON_DEEP_SLEEP_AWAKE
    return DTG_RST_DEEPSLEEP;
  case 6: // REASON_EXT_SYS_RST
    return DTG_RST_EXT;
  default:
    return DTG_RST_UNKNOWN;
  }
}
