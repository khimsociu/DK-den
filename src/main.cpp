#include <Arduino.h>
#include <dtg.h>
#include "token.h"

#define LED1 14
#define LED2 12
#define LED3 13
#define LEDstt 16

#define BTN1 5
#define BTN2 4
#define BTN3 0   // GPIO0: khong giu nut nay khi cap nguon/reset

const bool INVERT_LED = true;

const uint32_t SCAN_TICKS = 25000;      // 5 ms (80MHz / 16 = 5MHz)
const uint8_t DEBOUNCE_SAMPLES = 10;    // 10 x 5 ms = 50 ms

DtgWiFi dtg(PROJECT_ID, GW_ALIAS, MQTT_USER, MQTT_PASS, CLIENT_ID);

const uint8_t LED_PINS[3] = {LED1, LED2, LED3};
const uint8_t BTN_PINS[3] = {BTN1, BTN2, BTN3};
const char *const KEYS[3] = {"sw1", "sw2", "sw3"};

// Bien dung chung giua ngat va loop
volatile bool ledState[3] = {false, false, false};
volatile uint8_t pendingMask = 0;       // kenh can gui len Hub

// Chi dung trong ngat
bool stableState[3] = {HIGH, HIGH, HIGH};
uint8_t diffCount[3] = {0, 0, 0};

bool lastDtgConnected = false;

void IRAM_ATTR writeLed(uint8_t pin, bool logicalOn) {
  bool physicalLevel = INVERT_LED ? !logicalOn : logicalOn;
  digitalWrite(pin, physicalLevel ? HIGH : LOW);
}

// ===== Ngat quet nut: khong dung delay hay MQTT trong nay =====
void IRAM_ATTR scanButtons() {
  for (uint8_t i = 0; i < 3; i++) {
    bool reading = digitalRead(BTN_PINS[i]);

    if (reading == stableState[i]) {
      diffCount[i] = 0;
      continue;
    }

    if (++diffCount[i] >= DEBOUNCE_SAMPLES) {
      stableState[i] = reading;
      diffCount[i] = 0;

      if (reading == LOW) {
        ledState[i] = !ledState[i];
        writeLed(LED_PINS[i], ledState[i]);
        pendingMask |= (1 << i);
      }
    }
  }
}

void publishState(uint8_t i) {
  if (!dtg.connected()) return;

  int val = ledState[i] ? 1 : 0;
  dtg.publish(KEYS[i], val, true);
}

// Lenh tu Hub
void setChannel(uint8_t i, bool on) {
  noInterrupts();
  ledState[i] = on;
  writeLed(LED_PINS[i], on);
  interrupts();
  publishState(i);
}

// Gui cac thay doi do nut nhan
void sendPendingButtons() {
  noInterrupts();
  uint8_t mask = pendingMask;
  pendingMask = 0;
  interrupts();

  for (uint8_t i = 0; i < 3; i++) {
    if (mask & (1 << i)) publishState(i);
  }
}

// ===== Lenh dieu khien tu Hub =====
DTG_LISTEN("sw1") {
  setChannel(0, param.getBool());
}

DTG_LISTEN("sw2") {
  setChannel(1, param.getBool());
}

DTG_LISTEN("sw3") {
  setChannel(2, param.getBool());
}

DTG_CONNECTED() {
  for (uint8_t i = 0; i < 3; i++) publishState(i);
}

void setup() {
  for (uint8_t i = 0; i < 3; i++) {
    pinMode(LED_PINS[i], OUTPUT);
    writeLed(LED_PINS[i], ledState[i]);
    pinMode(BTN_PINS[i], INPUT_PULLUP);
  }
  pinMode(LEDstt, OUTPUT);
  writeLed(LEDstt, false);

  // Bat quet nut truoc khi ket noi mang
  timer1_attachInterrupt(scanButtons);
  timer1_enable(TIM_DIV16, TIM_EDGE, TIM_LOOP);
  timer1_write(SCAN_TICKS);

  dtg.begin(WIFI_SSID, WIFI_PASS);
  dtg.enableHubCmd();
}

void loop() {
  dtg.run();
  sendPendingButtons();

  bool curConn = dtg.connected();
  if (curConn != lastDtgConnected) {
    lastDtgConnected = curConn;
    writeLed(LEDstt, curConn);
  }
}