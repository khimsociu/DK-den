/**
 * @file ESP8266_WiFi_Uplink.ino
 * @brief Read a button and publish its state to DTG IoT Hub (ESP8266 / esp12e).
 *
 * Platform side:
 *   Create one datastream named "button" (type: integer, values: 0 or 1).
 *
 * Board: NodeMCU / Wemos D1 mini / ESP-12E. BUTTON_PIN GPIO4 = D2.
 * Flash: use a dual-OTA layout if you later enable Hub FOTA (e.g. 4M/2M).
 */
#define DTG_PROFILE_MQTT_ONLY 1
#include <dtg.h>

const char WIFI_SSID[] = "YOUR_WIFI_SSID";
const char WIFI_PASS[] = "YOUR_WIFI_PASSWORD";

const char PROJECT_ID[] = "12345";
const char GW_ALIAS[]   = "gw01";
const char MQTT_USER[]  = "YOUR_GATEWAY_MQTT_TOKEN";
const char MQTT_PASS[]  = "YOUR_GATEWAY_MQTT_TOKEN";
const char CLIENT_ID[]  = "gw-YOUR-GATEWAY-UUID";

// GPIO4 (D2). Avoid GPIO0 (BOOT) as a user button.
const int BUTTON_PIN = 4;

DtgWiFi dtg(PROJECT_ID, GW_ALIAS, MQTT_USER, MQTT_PASS, CLIENT_ID);

DTG_CONNECTED()
{
  dtgSerialLogConnected(dtg, CLIENT_ID);
}

void setup()
{
  Serial.begin(115200);
  delay(300);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  dtg.setLogStream(&Serial);
  dtg.begin(WIFI_SSID, WIFI_PASS);
}

void loop()
{
  dtg.run();

  static int lastState = -1;
  const int state = (digitalRead(BUTTON_PIN) == LOW) ? 1 : 0;

  if (state != lastState)
  {
    lastState = state;
    if (dtg.connected())
      dtg.publish("button", state);
  }
}
