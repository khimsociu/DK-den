/**
 * @file dtg_platform.h
 * @brief Thin ESP32 / ESP8266 shims (inline). No HAL class, no virtuals.
 */
#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <string.h>

#include "dtg_reset_reason_map.h"

#if defined(ESP32)
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_wifi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <user_interface.h>
#endif

inline uint32_t dtgRandom32()
{
#if defined(ESP32)
  return esp_random();
#elif defined(ESP8266)
  return RANDOM_REG32;
#else
  return static_cast<uint32_t>(random(0x7fffffff));
#endif
}

inline void dtgWdtFeed()
{
#if defined(ESP32)
  esp_task_wdt_reset();
#elif defined(ESP8266)
  ESP.wdtFeed();
  yield();
#endif
}

/** Arm task/hw watchdog. timeoutS==0 disables. ESP8266 WDT period is fixed (~few seconds). */
inline void dtgWdtArm(uint32_t timeoutS)
{
#if defined(ESP32)
  if (timeoutS == 0)
  {
    esp_task_wdt_delete(NULL);
    return;
  }
#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
  const esp_task_wdt_config_t wdtCfg = {.timeout_ms = timeoutS * 1000U,
                                        .idle_core_mask = 0,
                                        .trigger_panic = true};
  esp_task_wdt_reconfigure(&wdtCfg);
#else
  esp_task_wdt_init(timeoutS, true);
#endif
  esp_task_wdt_add(NULL);
#elif defined(ESP8266)
  if (timeoutS == 0)
  {
    ESP.wdtDisable();
    return;
  }
  // ponytail: ESP8266 HW WDT is ~6-8s fixed; soft WDT ~3.2s needs yield/feed in long loops.
  ESP.wdtEnable(WDTO_8S);
  (void)timeoutS;
#endif
}

inline void dtgWdtDisarm()
{
#if defined(ESP32)
  esp_task_wdt_delete(NULL);
#elif defined(ESP8266)
  ESP.wdtDisable();
#endif
}

inline uint32_t dtgFreeHeap()
{
  return ESP.getFreeHeap();
}

inline uint32_t dtgMinFreeHeap()
{
#if defined(ESP32)
  return ESP.getMinFreeHeap();
#elif defined(ESP8266)
  // Sampled watermark — ESP8266 core has no getMinFreeHeap().
  static uint32_t s_min = 0xffffffffu;
  const uint32_t now = ESP.getFreeHeap();
  if (now < s_min)
    s_min = now;
  return s_min;
#else
  return ESP.getFreeHeap();
#endif
}

inline uint32_t dtgLargestFreeBlock()
{
#if defined(ESP32)
  return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
#elif defined(ESP8266)
  return ESP.getMaxFreeBlockSize();
#else
  return ESP.getFreeHeap();
#endif
}

inline uint8_t dtgResetReason()
{
#if defined(ESP32)
  return static_cast<uint8_t>(esp_reset_reason());
#elif defined(ESP8266)
  const rst_info *info = system_get_rst_info();
  return dtgMapEsp8266ResetReason(info ? static_cast<uint8_t>(info->reason) : 0);
#else
  return DTG_RST_UNKNOWN;
#endif
}

inline bool dtgSetCpuMhz(uint32_t mhz)
{
#if defined(ESP32)
  return setCpuFrequencyMhz(mhz);
#elif defined(ESP8266)
  // ESP8266 Arduino 3.1.x: system_update_cpu_freq (80 or 160 only).
  if (mhz >= 160)
    return system_update_cpu_freq(SYS_CPU_160MHZ);
  return system_update_cpu_freq(SYS_CPU_80MHZ);
#else
  (void)mhz;
  return false;
#endif
}

inline unsigned dtgCpuMhz()
{
#if defined(ESP32)
  return static_cast<unsigned>(getCpuFrequencyMhz());
#elif defined(ESP8266)
  return static_cast<unsigned>(system_get_cpu_freq());
#else
  return 0u;
#endif
}

inline void dtgWifiDisconnect(bool wifiOff)
{
#if defined(ESP32)
  WiFi.disconnect(wifiOff, true);
#elif defined(ESP8266)
  WiFi.disconnect(wifiOff);
#else
  (void)wifiOff;
#endif
}

/** Copy current STA SSID into buf (NUL-terminated). Returns false if not associated. */
inline bool dtgWifiSsid(char *buf, size_t bufLen)
{
  if (!buf || bufLen < 2)
    return false;
  buf[0] = '\0';
  if (WiFi.status() != WL_CONNECTED)
    return false;
#if defined(ESP32)
  wifi_ap_record_t info{};
  if (esp_wifi_sta_get_ap_info(&info) != ESP_OK)
    return false;
  const size_t n = strnlen(reinterpret_cast<const char *>(info.ssid), 32);
  if (n == 0 || n >= bufLen)
    return false;
  memcpy(buf, info.ssid, n);
  buf[n] = '\0';
  return true;
#elif defined(ESP8266)
  const String ssid = WiFi.SSID();
  if (ssid.length() == 0 || static_cast<size_t>(ssid.length()) >= bufLen)
    return false;
  memcpy(buf, ssid.c_str(), static_cast<size_t>(ssid.length()) + 1);
  return true;
#else
  return false;
#endif
}

inline void dtgRestart()
{
  ESP.restart();
}
