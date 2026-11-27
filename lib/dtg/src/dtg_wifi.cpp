/**
 * @file dtg_wifi.cpp
 */
#include "dtg_wifi.h"

#include "dtg_debug.h"
#include "dtg_platform.h"
#include "dtg_wifi_store.h"

#include <new>
#include <stdio.h>
#include <string.h>

#if defined(ESP8266)
#include <time.h>
#endif

namespace
{

bool dtgWifiGetIp(char *buf, size_t bufLen, void *userData)
{
  (void)userData;
  if (!buf || bufLen < 8 || WiFi.status() != WL_CONNECTED)
    return false;
  const IPAddress ip = WiFi.localIP();
  const int n = snprintf(buf, bufLen, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  return n > 0 && (size_t)n < bufLen;
}

bool dtgWifiGetMac(char *buf, size_t bufLen, void *userData)
{
  (void)userData;
  if (!buf || bufLen < 18)
    return false;
  uint8_t mac[6] = {};
  WiFi.macAddress(mac);
  const int n = snprintf(buf, bufLen, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],
                         mac[1], mac[2], mac[3], mac[4], mac[5]);
  return n > 0 && (size_t)n < bufLen;
}

bool dtgWifiGetRssi(int *rssiDbm, void *userData)
{
  (void)userData;
  if (!rssiDbm || WiFi.status() != WL_CONNECTED)
    return false;
  const int rssi = WiFi.RSSI();
  if (rssi >= 0)
    return false;
  *rssiDbm = rssi;
  return true;
}

bool dtgWifiGetSsid(char *buf, size_t bufLen, void *userData)
{
  (void)userData;
  return dtgWifiSsid(buf, bufLen);
}

} // namespace

void DtgWiFi::installWifiRuntimeProvider()
{
  wifiRuntimeProvider_.getIp = dtgWifiGetIp;
  wifiRuntimeProvider_.getMac = dtgWifiGetMac;
  wifiRuntimeProvider_.getSsid = dtgWifiGetSsid;
  wifiRuntimeProvider_.getRssi = dtgWifiGetRssi;
  wifiRuntimeProvider_.networkType = "wifi";
  wifiRuntimeProvider_.userData = nullptr;
  setRuntimeInfoProvider(&wifiRuntimeProvider_);
}

DtgWiFi::DtgWiFi(const char *projectId, const char *gwAlias, const char *mqttUser,
                 const char *mqttPass, const char *clientId)
    : DtgClient(projectId, gwAlias, mqttUser, mqttPass, clientId)
{
  provisioner_.attach(this);
}

void DtgWiFi::setBootConfigPin(uint8_t pin)
{
  provisioner_.setBootConfigPin(pin);
  pinMode(pin, INPUT_PULLUP);
}

void DtgWiFi::applyPreferredDns()
{
  IPAddress dns1(DTG_DNS_PRIMARY_A, DTG_DNS_PRIMARY_B, DTG_DNS_PRIMARY_C,
                 DTG_DNS_PRIMARY_D);
  IPAddress dns2(DTG_DNS_SECONDARY_A, DTG_DNS_SECONDARY_B, DTG_DNS_SECONDARY_C,
                 DTG_DNS_SECONDARY_D);
#if defined(ESP8266)
  // local/gw/mask = 0 keeps DHCP; only DNS servers are overridden.
  const bool ok = WiFi.config(0U, 0U, 0U, dns1, dns2);
#else
  const bool ok =
      WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), dns1, dns2);
#endif
  DTG_LOG("Wi-Fi DNS override %s", ok ? "ok" : "failed");
}

#if defined(ESP8266)
void DtgWiFi::releaseTrustAnchors()
{
  delete trustAnchors_;
  trustAnchors_ = nullptr;
  trustAnchorsReady_ = false;
}

void DtgWiFi::ensureTrustAnchors()
{
  if (trustAnchorsReady_ && trustAnchors_)
    return;
  releaseTrustAnchors();

  const char *pem = caCert();
  if (!pem || pgm_read_byte(pem) == 0)
    return;

  // BearSSL reads PEM from RAM — PROGMEM bytes must be copied once.
  const size_t len = strlen_P(pem);
  char *ram = static_cast<char *>(malloc(len + 1));
  if (!ram)
  {
    DTG_LOG("TLS: CA malloc failed len=%u", static_cast<unsigned>(len));
    return;
  }
  memcpy_P(ram, pem, len);
  ram[len] = '\0';

  trustAnchors_ = new (std::nothrow) BearSSL::X509List(ram);
  free(ram);
  if (!trustAnchors_)
  {
    DTG_LOG("TLS: X509List alloc failed");
    return;
  }
  tlsClient_.setTrustAnchors(trustAnchors_);
  trustAnchorsReady_ = true;
}
#endif

void DtgWiFi::beginStaProfile(const DtgWifiCredentials &cred)
{
  if (!cred.configured)
    return;
  WiFi.mode(WIFI_STA);
  dtgWifiDisconnect(true);
#if defined(ESP8266)
  WiFi.config(0, 0, 0);
#else
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
#endif
  WiFi.begin(cred.ssid, cred.pass);
  staFailSinceMs_ = millis();
  dnsApplied_ = false;
#if defined(ESP8266)
  ntpStarted_ = false;
#endif
  DTG_LOG("Wi-Fi connecting to %s...", cred.ssid);
}

void DtgWiFi::begin(const char *wifiSsid, const char *wifiPass, bool useTls,
                    bool tlsInsecure)
{
  applyWatchdogDefaultIfUnset();
  useTlsTransport_ = useTls;
  setUseTls(useTls);
  setTlsInsecure(tlsInsecure);
  printBootBannerOnce();

  if (!mqttBrokerConfigured_)
  {
    const uint16_t port =
        useTls ? static_cast<uint16_t>(DTG_MQTT_PORT_TLS)
               : static_cast<uint16_t>(DTG_MQTT_PORT_PLAIN);
    setMqttEndpoint(DTG_MQTT_HOST, port);
  }

  if (useTlsTransport_)
    attachNetworkClient(tlsClient_);
  else
    attachNetworkClient(plainClient_);

#if defined(ESP8266)
  WiFi.persistent(false);
#endif

  dtgWifiStoreBegin();
#if DTG_WIFI_FORCE_SKETCH_CREDS
  if (wifiSsid && wifiSsid[0] != '\0' && dtgWifiValidateSsid(wifiSsid) &&
      dtgWifiValidatePass(wifiPass))
  {
    (void)dtgWifiStoreSavePrimary(wifiSsid, wifiPass ? wifiPass : "");
    DTG_LOG("Wi-Fi credentials synced from sketch (force)");
  }
#else
  (void)dtgWifiStoreSeedPrimaryIfEmpty(wifiSsid, wifiPass);
#endif

  DtgWifiCredentials pri{};
  if (!dtgWifiStoreLoadPrimary(&pri))
  {
    DTG_LOG("No primary Wi-Fi in NVS - using sketch credentials");
    strlcpy(pri.ssid, wifiSsid ? wifiSsid : "", sizeof(pri.ssid));
    strlcpy(pri.pass, wifiPass ? wifiPass : "", sizeof(pri.pass));
    pri.configured = pri.ssid[0] != '\0';
  }

  provisioner_.setState(DtgWifiState::StaPrimary);
  beginStaProfile(pri);
#if defined(ESP8266)
  WiFi.setSleepMode(WIFI_MODEM_SLEEP);
#else
  WiFi.setSleep(true);
#endif
  installWifiRuntimeProvider();
#if DTG_WIFI_BOOT_PIN != 255
  setBootConfigPin(DTG_WIFI_BOOT_PIN);
#endif
}

void DtgWiFi::maintainSta(unsigned long nowMs)
{
  const DtgWifiState st = provisioner_.state();
  if (st == DtgWifiState::ApSetup)
    return;

  if (WiFi.status() == WL_CONNECTED)
  {
    if (!dnsApplied_)
    {
      applyPreferredDns();
      dnsApplied_ = true;
    }
#if defined(ESP8266)
    if (useTlsTransport_ && !tlsInsecure_ && !ntpStarted_)
    {
      configTime(0, 0, "pool.ntp.org", "time.nist.gov");
      ntpStarted_ = true;
      DTG_LOG("NTP started for TLS time validation");
    }
#endif
    staFailSinceMs_ = 0;

    if (st == DtgWifiState::OfflineWatch)
    {
      provisioner_.setState(DtgWifiState::StaPrimary);
    }
    return;
  }

  if (staFailSinceMs_ == 0)
    staFailSinceMs_ = nowMs;

  if (st == DtgWifiState::StaPrimary &&
      nowMs - staFailSinceMs_ >= DTG_WIFI_FAILOVER_MS)
  {
    DtgWifiCredentials bak{};
    if (dtgWifiStoreLoadBackup(&bak))
    {
      DTG_LOG("Primary timeout - failing over to backup Wi-Fi");
      provisioner_.setState(DtgWifiState::StaBackup);
      beginStaProfile(bak);
      return;
    }
    provisioner_.setState(DtgWifiState::OfflineWatch);
    staFailSinceMs_ = nowMs;
    DTG_LOG("Primary timeout - no backup, offline watch");
    return;
  }

  if (st == DtgWifiState::StaBackup &&
      nowMs - staFailSinceMs_ >= DTG_WIFI_FAILOVER_MS)
  {
    DtgWifiCredentials pri{};
    if (dtgWifiStoreLoadPrimary(&pri))
    {
      DTG_LOG("Backup timeout - retrying primary Wi-Fi");
      provisioner_.setState(DtgWifiState::StaPrimary);
      beginStaProfile(pri);
      return;
    }
    provisioner_.setState(DtgWifiState::OfflineWatch);
    staFailSinceMs_ = nowMs;
    DTG_LOG("Backup timeout - no primary, offline watch");
    return;
  }

  if (st == DtgWifiState::OfflineWatch &&
      nowMs - staFailSinceMs_ >= DTG_WIFI_FAILOVER_MS)
  {
    DtgWifiCredentials pri{};
    if (dtgWifiStoreLoadPrimary(&pri))
    {
      DTG_LOG("Offline watch - retry primary Wi-Fi (%s)", pri.ssid);
      provisioner_.setState(DtgWifiState::StaPrimary);
      beginStaProfile(pri);
      staFailSinceMs_ = nowMs;
    }
  }
}

bool DtgWiFi::netLayerReady() const
{
  if (WiFi.status() != WL_CONNECTED)
    return false;
#if defined(ESP8266)
  // BearSSL requires wall clock for cert expiry checks (unless insecure).
  if (useTlsTransport_ && !tlsInsecure_)
  {
    const time_t now = time(nullptr);
    if (now < 1700000000) // ~2023-11 — reject unset/epoch time
      return false;
  }
#endif
  return true;
}

bool DtgWiFi::run()
{
  const unsigned long now = millis();
  const bool link = netLayerReady();
  const bool mqttUp = connected();

  provisioner_.tick(now, WiFi.status() == WL_CONNECTED, mqttUp);

  if (provisioner_.state() == DtgWifiState::ApSetup)
  {
    (void)provisioner_.handleApSetup(now);
    loopIdle();
    return false;
  }

  maintainSta(now);
  return DtgClient::run();
}

void DtgWiFi::configureTransportBeforeMqttConnect()
{
  if (!useTlsTransport_)
    return;

#if defined(ESP8266)
  // Giam bo dem TLS de tiet kiem RAM (chi do server 1 lan)
  static bool bufferReady = false;
  if (!bufferReady)
  {
    const bool mfln = BearSSL::WiFiClientSecure::probeMaxFragmentLength(
        DTG_MQTT_HOST, DTG_MQTT_PORT_TLS, 1024);
    const int rxSize = mfln ? 1024 : 4096;
    tlsClient_.setBufferSizes(rxSize, 1024);
    DTG_LOG("TLS: mfln=%d rx=%d heap=%u", mfln ? 1 : 0, rxSize,
            static_cast<unsigned>(ESP.getFreeHeap()));
    bufferReady = true;
  }
#endif

  if (tlsInsecure_)
  {
    tlsClient_.setInsecure();
    return;
  }
#if defined(ESP8266)
  ensureTrustAnchors();
#else
  tlsClient_.setCACert(caCert());
#endif
}