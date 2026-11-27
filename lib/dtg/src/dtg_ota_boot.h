/**
 * @file dtg_ota_boot.h
 * @brief Post-OTA self-test: confirm image after MQTT reconnect, else rollback.
 *
 * ESP32: Requires CONFIG_APP_ROLLBACK_ENABLE (dual-OTA partitions) when
 * DTG_OTA_ROLLBACK_GUARD=1. ESP8266: confirm-only (no A/B rollback).
 * Call dtgOtaBootOnMqttConnected() from connectMqttSession success path.
 * Call dtgOtaBootTick(millis()) from DtgClient::run() while pending.
 */
#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "dtg_config.h"

#if DTG_FEATURE_OTA

class PubSubClient;

namespace detail
{

/** Persist job before reboot so new image can report success / rolled_back. */
bool dtgOtaBootSavePending(const char *jobId, uint32_t targetPartitionAddr);

/** After MQTT connect: mark valid + publish success, or report rolled_back. */
void dtgOtaBootOnMqttConnected(PubSubClient &mqtt, const char *hubTopicPrefix);

/** While pending verify and MQTT still down: rollback after timeout. */
void dtgOtaBootTick(unsigned long nowMs);

} // namespace detail

#else

class PubSubClient;

namespace detail
{

inline bool dtgOtaBootSavePending(const char *, uint32_t) { return false; }
inline void dtgOtaBootOnMqttConnected(PubSubClient &, const char *) {}
inline void dtgOtaBootTick(unsigned long) {}

} // namespace detail

#endif
