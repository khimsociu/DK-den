/**
 * Host self-check for dtg_core_cfg_logic.h
 * g++ -std=c++17 -I../src -o core_cfg_logic_test core_cfg_logic_test.cpp
 */
#include <cassert>
#include <cstdio>

#include "../src/dtg_core_cfg_logic.h"

int main()
{
  assert(dtgClampPeriodUplinkMs(0) == DTG_PERIOD_UPLINK_MIN_MS);
  assert(dtgClampPeriodUplinkMs(500) == DTG_PERIOD_UPLINK_MIN_MS);
  assert(dtgClampPeriodUplinkMs(5000) == 5000U);
  assert(dtgClampPeriodUplinkMs(DTG_PERIOD_UPLINK_MAX_MS + 1) ==
         DTG_PERIOD_UPLINK_MAX_MS);

  assert(dtgShouldApplyPeriodUplink(10, 5));
  assert(dtgShouldApplyPeriodUplink(5, 5));
  assert(!dtgShouldApplyPeriodUplink(4, 5));
  assert(dtgShouldApplyPeriodUplink(0, 0));

  std::puts("core_cfg_logic_test OK");
  return 0;
}
