#include "../src/dtg_reset_reason_map.h"

#include <cstdio>

#define EXPECT(cond)                                                           \
  do                                                                           \
  {                                                                            \
    if (!(cond))                                                               \
    {                                                                          \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
      return 1;                                                                \
    }                                                                          \
  } while (0)

int main()
{
  EXPECT(dtgMapEsp8266ResetReason(0) == DTG_RST_POWERON);
  EXPECT(dtgMapEsp8266ResetReason(1) == DTG_RST_WDT);
  EXPECT(dtgMapEsp8266ResetReason(2) == DTG_RST_PANIC);
  EXPECT(dtgMapEsp8266ResetReason(3) == DTG_RST_TASK_WDT);
  EXPECT(dtgMapEsp8266ResetReason(4) == DTG_RST_SW);
  EXPECT(dtgMapEsp8266ResetReason(5) == DTG_RST_DEEPSLEEP);
  EXPECT(dtgMapEsp8266ResetReason(6) == DTG_RST_EXT);
  EXPECT(dtgMapEsp8266ResetReason(99) == DTG_RST_UNKNOWN);
  std::printf("reset_reason_map_test OK\n");
  return 0;
}
