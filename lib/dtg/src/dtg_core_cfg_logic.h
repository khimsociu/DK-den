/**
 * @file dtg_core_cfg_logic.h
 * @brief Host-testable periodUplink clamp + version gate (Arduino-free).
 */
#pragma once

#include <stdint.h>

#ifndef DTG_PERIOD_UPLINK_DEFAULT_MS
#define DTG_PERIOD_UPLINK_DEFAULT_MS 5000U
#endif
#ifndef DTG_PERIOD_UPLINK_MIN_MS
#define DTG_PERIOD_UPLINK_MIN_MS 1000U
#endif
#ifndef DTG_PERIOD_UPLINK_MAX_MS
#define DTG_PERIOD_UPLINK_MAX_MS 3600000U
#endif

inline uint32_t dtgClampPeriodUplinkMs(uint32_t ms)
{
  if (ms < DTG_PERIOD_UPLINK_MIN_MS)
    return DTG_PERIOD_UPLINK_MIN_MS;
  if (ms > DTG_PERIOD_UPLINK_MAX_MS)
    return DTG_PERIOD_UPLINK_MAX_MS;
  return ms;
}

/**
 * Apply Hub periodUplink only when version is not older than last applied.
 * ponytail: if Hub configPublishSeq is reset below NVS pver, device ignores until seq catches up.
 */
inline bool dtgShouldApplyPeriodUplink(uint32_t version, uint32_t appliedPeriodVer)
{
  return version >= appliedPeriodVer;
}
