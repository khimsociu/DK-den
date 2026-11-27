/**
 * @file dtg_prefs.h
 * @brief Preferences-compatible store: ESP32 NVS alias, ESP8266 LittleFS file-per-key.
 */
#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#if defined(ESP32)

#include <Preferences.h>

using DtgPrefs = Preferences;

#else

/**
 * ESP8266: one LittleFS file per key under /p/<ns>/<key>.
 * Write-if-changed to limit flash wear. Keys/ns: [a-z0-9_], max 15 chars.
 */
class DtgPrefs
{
public:
  bool begin(const char *name, bool readOnly = false);
  void end();

  bool clear();
  bool remove(const char *key);

  size_t putUChar(const char *key, uint8_t value);
  size_t putUInt(const char *key, uint32_t value);
  size_t putBool(const char *key, bool value);
  size_t putString(const char *key, const char *value);
  size_t putBytes(const char *key, const void *value, size_t len);

  bool isKey(const char *key);

  uint8_t getUChar(const char *key, uint8_t defaultValue = 0);
  uint16_t getUShort(const char *key, uint16_t defaultValue = 0);
  uint32_t getUInt(const char *key, uint32_t defaultValue = 0);
  bool getBool(const char *key, bool defaultValue = false);
  size_t getString(const char *key, char *value, size_t maxLen);
  String getString(const char *key, const String &defaultValue = String());
  size_t getBytesLength(const char *key);
  size_t getBytes(const char *key, void *buf, size_t maxLen);

private:
  char ns_[16] = {};
  bool open_ = false;
  bool readOnly_ = false;

  bool validToken(const char *s) const;
  bool buildPath(const char *key, char *out, size_t outLen) const;
  bool ensureNsDir() const;
  bool readAll(const char *key, uint8_t *buf, size_t maxLen, size_t *outLen);
  size_t writeAll(const char *key, const uint8_t *data, size_t len);
};

#endif
