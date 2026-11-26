#include <Arduino.h>
#include <dtg.h>

// LED
#define LED1 14   // GPIO14 (D5)
#define LED2 12   // GPIO12 (D6)
#define LED3 13   // GPIO13 (D7)
#define LEDstt 16 // GPIO16 (D0) - LED status

// Button (active LOW with INPUT_PULLUP)
#define BTN1 5    // GPIO5  (D1)
#define BTN2 4    // GPIO4  (D2)
#define BTN3 0    // GPIO0  (D3) - BOOT pin, tránh giữ LOW lúc reset/boot

const bool INVERT_LED = true;
const unsigned long DEBOUNCE_MS = 50;

// 1) Wi-Fi credentials
const char WIFI_SSID[] = "YOUR_WIFI_SSID";
const char WIFI_PASS[] = "YOUR_WIFI_PASSWORD";

// 2) DTG IoT Hub project + gateway credentials
const char PROJEC T_ID[] = "12345";
const char GW_ALIAS[]   = "gw01";
const char MQTT_USER[]  = "YOUR_GATEWAY_MQTT_TOKEN";
const char MQTT_PASS[]  = "YOUR_GATEWAY_MQTT_TOKEN";
const char CLIENT_ID[]  = "gw-YOUR-GATEWAY-UUID";

DtgWiFi dtg(PROJECT_ID, GW_ALIAS, MQTT_USER, MQTT_PASS, CLIENT_ID);

bool stateLED1 = false;
bool stateLED2 = false;
bool stateLED3 = false;

bool lastReading1 = HIGH, lastReading2 = HIGH, lastReading3 = HIGH;
bool stableState1 = HIGH, stableState2 = HIGH, stableState3 = HIGH;
unsigned long lastDebounceTime1 = 0, lastDebounceTime2 = 0, lastDebounceTime3 = 0;

void writeLed(uint8_t pin, bool logicalOn) {
  bool physicalLevel = INVERT_LED ? !logicalOn : logicalOn;
  digitalWrite(pin, physicalLevel ? HIGH : LOW);
}

void publishAllStates() {
  if (!dtg.connected()) return;
  dtg.publish("sw1", stateLED1 ? 1 : 0);
  dtg.publish("sw2", stateLED2 ? 1 : 0);
  dtg.publish("sw3", stateLED3 ? 1 : 0);
}

void publishOneState(uint8_t index, bool isOn) {
  if (!dtg.connected()) return;
  if (index == 1) dtg.publish("sw1", isOn ? 1 : 0);
  else if (index == 2) dtg.publish("sw2", isOn ? 1 : 0);
  else if (index == 3) dtg.publish("sw3", isOn ? 1 : 0);
}

DTG_CONNECTED() {
  dtgSerialLogConnected(dtg, CLIENT_ID);
  publishAllStates();
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);
  pinMode(LEDstt, OUTPUT);

  writeLed(LED1, stateLED1);
  writeLed(LED2, stateLED2);
  writeLed(LED3, stateLED3);
  digitalWrite(LEDstt, HIGH);

  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BTN3, INPUT_PULLUP);

  dtg.setLogStream(&Serial);
  dtg.begin(WIFI_SSID, WIFI_PASS);
}

void loop() {
  dtg.run();

  unsigned long now = millis();

  // --- BTN1 ---
  bool r1 = digitalRead(BTN1);
  if (r1 != lastReading1) lastDebounceTime1 = now;
  if ((now - lastDebounceTime1) > DEBOUNCE_MS && r1 != stableState1) {
    stableState1 = r1;
    if (stableState1 == LOW) {
      stateLED1 = !stateLED1;
      writeLed(LED1, stateLED1);
      Serial.print("BTN1 -> sw1=");
      Serial.println(stateLED1 ? 1 : 0);
      publishOneState(1, stateLED1);
    }
  }
  lastReading1 = r1;

  // --- BTN2 ---
  bool r2 = digitalRead(BTN2);
  if (r2 != lastReading2) lastDebounceTime2 = now;
  if ((now - lastDebounceTime2) > DEBOUNCE_MS && r2 != stableState2) {
    stableState2 = r2;
    if (stableState2 == LOW) {
      stateLED2 = !stateLED2;
      writeLed(LED2, stateLED2);
      Serial.print("BTN2 -> sw2=");
      Serial.println(stateLED2 ? 1 : 0);
      publishOneState(2, stateLED2);
    }
  }
  lastReading2 = r2;

  // --- BTN3 (GPIO0) ---
  bool r3 = digitalRead(BTN3);
  if (r3 != lastReading3) lastDebounceTime3 = now;
  if ((now - lastDebounceTime3) > DEBOUNCE_MS && r3 != stableState3) {
    stableState3 = r3;
    if (stableState3 == LOW) {
      stateLED3 = !stateLED3;
      writeLed(LED3, stateLED3);
      Serial.print("BTN3 -> sw3=");
      Serial.println(stateLED3 ? 1 : 0);
      publishOneState(3, stateLED3);
    }
  }
  lastReading3 = r3;
}