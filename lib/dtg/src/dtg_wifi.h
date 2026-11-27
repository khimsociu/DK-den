/**
 * @file dtg_wifi.h
 * @brief Wi-Fi transport for DTG IoT Hub (TLS 8883 or plain 1883).
 * ESP32: mbedTLS WiFiClientSecure. ESP8266: BearSSL WiFiClientSecure.
 */
#pragma once

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#else
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#endif

#include "dtg_client.h"
#include "dtg_wifi_provision.h"

class DtgWiFi : public DtgClient
{
public:
  DtgWiFi(const char *projectId, const char *gwAlias, const char *mqttUser,
          const char *mqttPass, const char *clientId);

  /**
   * Connect to AP then MQTT broker using platform defaults from dtg_config.h.
   * Non-blocking: Wi-Fi/MQTT finish in run().
   */
  void begin(const char *wifiSsid, const char *wifiPass, bool useTls = true,
             bool tlsInsecure = false);

  bool run() override;

  bool netLayerReady() const override;

  /** GPIO for long-press setup (default 0 = BOOT). */
  void setBootConfigPin(uint8_t pin);

protected:
  void configureTransportBeforeMqttConnect() override;

private:
  WiFiClientSecure tlsClient_;
  WiFiClient plainClient_;
  bool useTlsTransport_ = true;
  DtgRuntimeInfoProvider wifiRuntimeProvider_{};
  DtgWifiProvisioner provisioner_{};

  unsigned long staFailSinceMs_ = 0;
  bool dnsApplied_ = false;
#if defined(ESP8266)
  bool ntpStarted_ = false;
  bool trustAnchorsReady_ = false;
  BearSSL::X509List *trustAnchors_ = nullptr;

  void ensureTrustAnchors();
  void releaseTrustAnchors();
#endif

  void installWifiRuntimeProvider();
  void maintainSta(unsigned long nowMs);
  void beginStaProfile(const DtgWifiCredentials &cred);

  static void applyPreferredDns();
};
