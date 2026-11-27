/**
 * @file dtg_heap_guard.cpp
 */
#include "dtg_heap_guard.h"

#include "dtg_debug.h"
#include "dtg_heap_guard_logic.h"
#include "dtg_modem_lifecycle.h"
#include "dtg_platform.h"
#include "dtg_prefs.h"

#include <time.h>

namespace
{

static DtgHeapGuardDeferFn s_deferPredicate = nullptr;
static DtgHeapGuardPrepareRestartFn s_prepareHandler = nullptr;
static bool s_otaInProgress = false;
static DtgHeapGuardLogicState s_logicState = {};
static const char *const kNvsNs = "dtg_heap";
static const char *const kCooldownKey = "gr_u32";
static const uint32_t kCooldownNoTimeMagic = 0xFFFFFFFFu;
/** 0 = not loaded this boot; 1 = s_cachedStamp valid. */
static int8_t s_cooldownLoaded = 0;
static uint32_t s_cachedStamp = 0;

static void persistGuardRestartCooldown()
{
  DtgPrefs prefs;
  if (!prefs.begin(kNvsNs, false))
    return;
  const time_t now = time(nullptr);
  s_cachedStamp =
      (now > 100000) ? static_cast<uint32_t>(now) : kCooldownNoTimeMagic;
  prefs.putUInt(kCooldownKey, s_cachedStamp);
  prefs.end();
  s_cooldownLoaded = 1;
}

static bool cooldownActive(unsigned long nowMs)
{
  // RW begin creates empty ns — RO begin ERROR-logs NOT_FOUND on ESP32 NVS.
  // Cache stamp so tele/snap (~60s) does not re-open store.
  if (s_cooldownLoaded == 0)
  {
    DtgPrefs prefs;
    if (prefs.begin(kNvsNs, false))
    {
      s_cachedStamp = prefs.getUInt(kCooldownKey, 0);
      prefs.end();
    }
    else
    {
      s_cachedStamp = 0;
    }
    s_cooldownLoaded = 1;
  }
  if (s_cachedStamp == 0)
    return false;
  if (s_cachedStamp == kCooldownNoTimeMagic)
    return nowMs < DTG_HEAP_GUARD_COOLDOWN_MS;
  const time_t now = time(nullptr);
  if (now <= 100000)
    return nowMs < DTG_HEAP_GUARD_COOLDOWN_MS;
  return (static_cast<uint32_t>(now) - s_cachedStamp) <
         (DTG_HEAP_GUARD_COOLDOWN_MS / 1000UL);
}

} // namespace

void dtgHeapGuardSetDeferPredicate(DtgHeapGuardDeferFn fn) { s_deferPredicate = fn; }

void dtgHeapGuardSetPrepareRestartHandler(DtgHeapGuardPrepareRestartFn fn)
{
  s_prepareHandler = fn;
}

void dtgHeapGuardSetOtaInProgress(bool inProgress) { s_otaInProgress = inProgress; }

bool dtgHeapGuardShouldDefer(unsigned long nowMs)
{
  if (s_otaInProgress)
    return true;
  if (nowMs < DTG_HEAP_GUARD_BOOT_GRACE_MS)
    return true;
  if (cooldownActive(nowMs))
    return true;
  if (s_deferPredicate && s_deferPredicate())
    return true;
  return false;
}

void dtgHeapGuardCheck(unsigned long nowMs)
{
  const bool defer = dtgHeapGuardShouldDefer(nowMs);
  const uint32_t freeHeap = dtgFreeHeap();
  const uint32_t largestBlk = dtgLargestFreeBlock();
  const bool low = dtgHeapGuardIsLow(freeHeap, largestBlk, DTG_HEAP_GUARD_MIN_FREE_BYTES,
                                    DTG_HEAP_GUARD_MIN_BLOCK_BYTES);
  bool shouldRestart = false;
  (void)dtgHeapGuardEvaluate(defer, low, DTG_HEAP_GUARD_CONSECUTIVE_CHECKS, &s_logicState,
                             &shouldRestart);
  if (!shouldRestart)
    return;

  DTG_LOG("heap guard: free=%lu largest_blk=%lu - controlled restart",
          static_cast<unsigned long>(freeHeap), static_cast<unsigned long>(largestBlk));
  persistGuardRestartCooldown();
  if (s_prepareHandler)
    s_prepareHandler();
  else
    dtgInvokeModemPrepareForRestart();
  dtgWdtFeed();
  delay(500);
  dtgRestart();
}
