/**
 * @file ESP8266_WiFi_Gateway.ino
 * @brief Hub gateway on ESP8266: no-code I/O + Hub cmd + FOTA (no A/B rollback).
 *
 * Flash map: eagle.flash.4m2m.ld (or equivalent) + LittleFS for credentials.
 * OTA has no hardware rollback on ESP8266 — stage rollouts carefully.
 *
 * Wi-Fi: hold BOOT (GPIO0) 20s or offline timeout to enter setup AP (dtg-gw-xxxxxx).
 */
#define DTG_WIFI_GATEWAY_ONLY 1
#include <dtg.h>

const char WIFI_SSID[] = "YOUR_WIFI_SSID";
const char WIFI_PASS[] = "YOUR_WIFI_PASSWORD";

const char PROJECT_ID[] = "12345";
const char GW_ALIAS[]   = "gw01";
const char MQTT_USER[]  = "YOUR_20_CHAR_GATEWAY_TOKEN";
const char MQTT_PASS[]  = "YOUR_20_CHAR_GATEWAY_TOKEN";
const char CLIENT_ID[]  = "gw-YOUR_GATEWAY_UUID";

const bool USE_TLS = true;

DtgWiFi dtg(PROJECT_ID, GW_ALIAS, MQTT_USER, MQTT_PASS, CLIENT_ID);

void setup()
{
  Serial.begin(115200);
  delay(120);

  dtg.setLogStream(&Serial);
  dtg.begin(WIFI_SSID, WIFI_PASS, USE_TLS);

  dtg.setFirmwareVersion(DTG_FIRMWARE_VERSION);
  dtg.enableHubCmd();
  dtg.enablePlatformFeatures();
}

void loop()
{
  dtg.run();
}
