/**
 * @file dtg_core_cfg.h
 * @brief Hub down/cfg core actions: periodUplink + MQTT credential rotate.
 *
 * Always active (not gated on no-code). Call from DtgClient::dispatchIncoming
 * before no-code/modbus config handlers.
 */
#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "dtg_config.h"

class DtgClient;

namespace detail
{

class HubCoreCfg
{
public:
  static HubCoreCfg &instance();

  void attach(DtgClient *client);
  void detach();

  uint32_t periodUplink() const;
  void setPeriodUplink(uint32_t ms);

  /** Handle down/cfg payload. Returns true if action was consumed (caller may skip chain). */
  bool onCfgPayload(const uint8_t *payload, unsigned int len);

private:
  HubCoreCfg() = default;

  void ensureLoaded();
  void persistPeriodUplink(uint32_t ms, uint32_t ver);
  bool publishAck(uint32_t version, const char *status, const char *msg);

  DtgClient *client_ = nullptr;
  bool loaded_ = false;
  uint32_t periodUplinkMs_ = DTG_PERIOD_UPLINK_DEFAULT_MS;
  uint32_t periodVer_ = 0;
};

} // namespace detail
