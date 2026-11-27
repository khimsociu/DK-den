/**
 * @file dtg_core_cfg.cpp
 */
#include "dtg_core_cfg.h"

#include "dtg_client.h"
#include "dtg_core_cfg_logic.h"
#include "dtg_debug.h"
#include "dtg_hub_cfg.h"

#include "dtg_prefs.h"

#include <ArduinoJson.h>
#include <string.h>

namespace detail
{

HubCoreCfg &HubCoreCfg::instance()
{
  static HubCoreCfg inst;
  return inst;
}

void HubCoreCfg::attach(DtgClient *client)
{
  if (client_ == client)
    return;
  client_ = client;
  loaded_ = false;
}

void HubCoreCfg::detach()
{
  client_ = nullptr;
  loaded_ = false;
}

void HubCoreCfg::ensureLoaded()
{
  if (loaded_)
    return;
  loaded_ = true;
  // RW begin creates empty ns if missing — avoids Preferences ERROR NOT_FOUND spam.
  DtgPrefs prefs;
  if (!prefs.begin(DTG_CFG_NVS_NS, false))
    return;
  periodUplinkMs_ =
      dtgClampPeriodUplinkMs(prefs.getUInt("pup", DTG_PERIOD_UPLINK_DEFAULT_MS));
  periodVer_ = prefs.getUInt("pver", 0);
  prefs.end();
}

uint32_t HubCoreCfg::periodUplink() const
{
  const_cast<HubCoreCfg *>(this)->ensureLoaded();
  return periodUplinkMs_;
}

void HubCoreCfg::persistPeriodUplink(uint32_t ms, uint32_t ver)
{
  DtgPrefs prefs;
  if (!prefs.begin(DTG_CFG_NVS_NS, false))
    return;
  prefs.putUInt("pup", ms);
  prefs.putUInt("pver", ver);
  prefs.end();
}

void HubCoreCfg::setPeriodUplink(uint32_t ms)
{
  ensureLoaded();
  periodUplinkMs_ = dtgClampPeriodUplinkMs(ms);
  persistPeriodUplink(periodUplinkMs_, periodVer_);
  DTG_LOG("[CFG] periodUplink=%u", (unsigned)periodUplinkMs_);
}

bool HubCoreCfg::publishAck(uint32_t version, const char *status, const char *msg)
{
  if (!client_ || !client_->connected())
    return false;
  JsonDocument doc;
  doc["version"] = version;
  doc["status"] = status;
  if (msg && msg[0])
    doc["msg"] = msg;
  char buf[192];
  const size_t n = serializeJson(doc, buf, sizeof(buf));
  if (n == 0 || n >= sizeof(buf))
    return false;
  return client_->publishTopicSuffix("up/cfg_ack", buf, true, 1);
}

bool HubCoreCfg::onCfgPayload(const uint8_t *payload, unsigned int len)
{
  if (!payload || len == 0)
    return false;
  ensureLoaded();

  JsonDocument filter;
  filter["action"] = true;
  filter["version"] = true;
  filter["periodUplink"] = true;
  filter["mqttUser"] = true;
  filter["mqttPass"] = true;

  JsonDocument doc;
  const DeserializationError derr =
      deserializeJson(doc, payload, len, DeserializationOption::Filter(filter));
  if (derr)
    return false;

  const char *action = doc["action"] | "";
  if (!action[0])
    return false;

  if (strcasecmp(action, "set_period_uplink") == 0)
  {
    const uint32_t ver = doc["version"] | 0U;
    if (!dtgShouldApplyPeriodUplink(ver, periodVer_))
    {
      publishAck(ver, "ignored", "stale_period_ver");
      return true;
    }
    const uint32_t ms = doc["periodUplink"] | DTG_PERIOD_UPLINK_DEFAULT_MS;
    periodUplinkMs_ = dtgClampPeriodUplinkMs(ms);
    periodVer_ = ver;
    persistPeriodUplink(periodUplinkMs_, periodVer_);
    DTG_LOG("[CFG] periodUplink=%u ver=%u", (unsigned)periodUplinkMs_,
            (unsigned)periodVer_);
    publishAck(ver, "ok", "period_uplink");
    return true;
  }

  if (strcasecmp(action, "set_mqtt_credential") == 0)
  {
    if (!client_)
      return true;
    const uint32_t ver = doc["version"] | 0U;
    const char *user = doc["mqttUser"] | "";
    const char *pass = doc["mqttPass"] | "";
    if (!user[0] || !pass[0])
    {
      publishAck(ver, "error", "missing_credential");
      return true;
    }
    if (!dtgHubCfgSaveCredentials(user, pass, ver))
    {
      publishAck(ver, "error", "nvs_write");
      return true;
    }
    client_->updateMqttCredentials(user, pass);
    publishAck(ver, "ok", "credential_rotated");
    return true;
  }

  // full_sync: apply periodUplink field only; leave stream handling to no-code.
  if (strcasecmp(action, "full_sync") == 0)
  {
    if (doc["periodUplink"].isNull())
      return false;
    const uint32_t ver = doc["version"] | 0U;
    if (!dtgShouldApplyPeriodUplink(ver, periodVer_))
      return false; // let no-code still process streams / ack
    const uint32_t ms = doc["periodUplink"] | DTG_PERIOD_UPLINK_DEFAULT_MS;
    periodUplinkMs_ = dtgClampPeriodUplinkMs(ms);
    periodVer_ = ver;
    persistPeriodUplink(periodUplinkMs_, periodVer_);
    DTG_LOG("[CFG] full_sync periodUplink=%u ver=%u", (unsigned)periodUplinkMs_,
            (unsigned)periodVer_);
    return false; // not consumed — no-code continues for streams
  }

  return false;
}

} // namespace detail
