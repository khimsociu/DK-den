/**
 * @file dtg_ota_boot.cpp
 */
#include "dtg_ota_boot.h"

#if DTG_FEATURE_OTA

#include "dtg_debug.h"
#include "dtg_prefs.h"

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <string.h>

#if defined(ESP32)
#include <esp_ota_ops.h>
#if DTG_OTA_ROLLBACK_GUARD
/** Defer core auto-confirm so we can self-test after MQTT reconnect. */
extern "C" bool verifyRollbackLater() { return true; }
#endif
#endif

namespace detail
{

namespace
{

#ifndef DTG_OTA_NVS_NS
#define DTG_OTA_NVS_NS "dtg_ota"
#endif

static bool s_pendingActive = false;
static bool s_checked = false;
static unsigned long s_bootMs = 0;
static char s_jobId[48] = {};
static uint32_t s_targetAddr = 0;

static bool publishStatus(PubSubClient &mqtt, const char *hubTopicPrefix, const char *jobId,
                          const char *status, int progress, const char *errorMsg)
{
  if (!hubTopicPrefix || !jobId || !jobId[0] || !mqtt.connected())
    return false;
  char t[128];
  snprintf(t, sizeof(t), "%sup/ota_status", hubTopicPrefix);
  JsonDocument doc;
  doc["jobId"] = jobId;
  doc["status"] = status;
  doc["progress"] = progress;
  if (errorMsg && errorMsg[0])
    doc["error"] = errorMsg;
  char buf[256];
  const size_t n = serializeJson(doc, buf, sizeof(buf));
  return mqtt.publish(t, reinterpret_cast<const uint8_t *>(buf), static_cast<unsigned int>(n),
                      false);
}

static void clearPendingNvs()
{
  DtgPrefs prefs;
  if (!prefs.begin(DTG_OTA_NVS_NS, false))
    return;
  prefs.clear();
  prefs.end();
  s_jobId[0] = '\0';
  s_targetAddr = 0;
  s_pendingActive = false;
}

static void loadPendingOnce()
{
  if (s_checked)
    return;
  s_checked = true;
  s_bootMs = millis();

  DtgPrefs prefs;
  if (!prefs.begin(DTG_OTA_NVS_NS, false))
    return;
  if (!prefs.isKey("job"))
  {
    prefs.end();
    return;
  }
  String jid = prefs.getString("job", "");
  s_targetAddr = prefs.isKey("part") ? prefs.getUInt("part", 0) : 0;
  prefs.end();
  if (jid.length() == 0 || jid.length() >= sizeof(s_jobId))
    return;
  strncpy(s_jobId, jid.c_str(), sizeof(s_jobId) - 1);
  s_jobId[sizeof(s_jobId) - 1] = '\0';
  s_pendingActive = true;
  DTG_LOG("[OTA] pending job=%s target=0x%lx", s_jobId,
          static_cast<unsigned long>(s_targetAddr));
}

} // namespace

bool dtgOtaBootSavePending(const char *jobId, uint32_t targetPartitionAddr)
{
  if (!jobId || !jobId[0])
    return false;
  DtgPrefs prefs;
  if (!prefs.begin(DTG_OTA_NVS_NS, false))
    return false;
  const bool ok = prefs.putString("job", jobId) > 0;
  prefs.putUInt("part", targetPartitionAddr);
  prefs.end();
  return ok;
}

void dtgOtaBootOnMqttConnected(PubSubClient &mqtt, const char *hubTopicPrefix)
{
  loadPendingOnce();
  if (!s_pendingActive)
    return;

#if defined(ESP32)
  const esp_partition_t *running = esp_ota_get_running_partition();
  const uint32_t runAddr = running ? running->address : 0;

  if (s_targetAddr != 0 && runAddr != 0 && runAddr != s_targetAddr)
  {
    DTG_LOG("[OTA] rolled_back running=0x%lx expected=0x%lx",
            static_cast<unsigned long>(runAddr), static_cast<unsigned long>(s_targetAddr));
    publishStatus(mqtt, hubTopicPrefix, s_jobId, "rolled_back", 0, "boot_rollback");
    clearPendingNvs();
    return;
  }

#if DTG_OTA_ROLLBACK_GUARD
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  if (running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
      state == ESP_OTA_IMG_PENDING_VERIFY)
  {
    const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    DTG_LOG("[OTA] mark_valid err=0x%x", static_cast<unsigned>(err));
  }
#endif
#else
  // ESP8266: no A/B partitions — confirm-only after MQTT reconnect.
  (void)s_targetAddr;
#endif

  publishStatus(mqtt, hubTopicPrefix, s_jobId, "success", 100, "");
  clearPendingNvs();
}

void dtgOtaBootTick(unsigned long nowMs)
{
  loadPendingOnce();
  if (!s_pendingActive)
    return;

#if defined(ESP32) && DTG_OTA_ROLLBACK_GUARD
  if (static_cast<long>(nowMs - s_bootMs) < static_cast<long>(DTG_OTA_CONFIRM_TIMEOUT_MS))
    return;

  DTG_LOG("[OTA] confirm timeout — rollback");
  // Leave NVS job so old image can report rolled_back after reboot.
  esp_ota_mark_app_invalid_rollback_and_reboot();
#else
  (void)nowMs;
#endif
}

} // namespace detail

#endif // DTG_FEATURE_OTA
