/**
 * @file dtg_prefs.cpp
 * @brief ESP8266 LittleFS Preferences shim (no-op compile unit on ESP32).
 */
#include "dtg_prefs.h"

#if defined(ESP8266)

#include <LittleFS.h>
#include <stdio.h>
#include <string.h>

namespace
{
bool s_fsReady = false;

bool ensureFs()
{
  if (s_fsReady)
    return true;
  s_fsReady = LittleFS.begin();
  return s_fsReady;
}
} // namespace

bool DtgPrefs::validToken(const char *s) const
{
  if (!s || !s[0])
    return false;
  size_t n = 0;
  for (const char *p = s; *p; ++p, ++n)
  {
    if (n >= 15)
      return false;
    const char c = *p;
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    if (!ok)
      return false;
  }
  return true;
}

bool DtgPrefs::buildPath(const char *key, char *out, size_t outLen) const
{
  if (!out || outLen < 8 || !validToken(ns_) || !validToken(key))
    return false;
  const int n = snprintf(out, outLen, "/p/%s/%s", ns_, key);
  return n > 0 && static_cast<size_t>(n) < outLen;
}

bool DtgPrefs::ensureNsDir() const
{
  if (!ensureFs() || !validToken(ns_))
    return false;
  if (!LittleFS.exists("/p") && !LittleFS.mkdir("/p"))
    return false;
  char dir[24];
  snprintf(dir, sizeof(dir), "/p/%s", ns_);
  if (LittleFS.exists(dir))
    return true;
  return LittleFS.mkdir(dir);
}

bool DtgPrefs::begin(const char *name, bool readOnly)
{
  if (!validToken(name))
    return false;
  if (!ensureFs())
    return false;
  strncpy(ns_, name, sizeof(ns_) - 1);
  ns_[sizeof(ns_) - 1] = '\0';
  readOnly_ = readOnly;
  if (!readOnly_ && !ensureNsDir())
    return false;
  open_ = true;
  return true;
}

void DtgPrefs::end()
{
  open_ = false;
  ns_[0] = '\0';
}

bool DtgPrefs::readAll(const char *key, uint8_t *buf, size_t maxLen, size_t *outLen)
{
  if (outLen)
    *outLen = 0;
  if (!open_ || !buf)
    return false;
  char path[48];
  if (!buildPath(key, path, sizeof(path)))
    return false;
  if (!LittleFS.exists(path))
    return false;
  File f = LittleFS.open(path, "r");
  if (!f)
    return false;
  const size_t n = f.read(buf, maxLen);
  f.close();
  if (outLen)
    *outLen = n;
  return true;
}

size_t DtgPrefs::writeAll(const char *key, const uint8_t *data, size_t len)
{
  if (!open_ || readOnly_ || !data)
    return 0;
  if (!ensureNsDir())
    return 0;
  char path[48];
  char tmp[52];
  if (!buildPath(key, path, sizeof(path)))
    return 0;
  // Write-if-changed: skip identical payload to reduce flash wear / DoS.
  if (LittleFS.exists(path))
  {
    File rf = LittleFS.open(path, "r");
    if (rf)
    {
      const size_t existing = rf.size();
      if (existing == len)
      {
        bool same = true;
        for (size_t i = 0; i < len && same; ++i)
        {
          const int b = rf.read();
          if (b < 0 || static_cast<uint8_t>(b) != data[i])
            same = false;
        }
        rf.close();
        if (same)
          return len;
      }
      else
      {
        rf.close();
      }
    }
  }
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  File wf = LittleFS.open(tmp, "w");
  if (!wf)
    return 0;
  const size_t written = wf.write(data, len);
  wf.close();
  if (written != len)
  {
    LittleFS.remove(tmp);
    return 0;
  }
  LittleFS.remove(path);
  if (!LittleFS.rename(tmp, path))
  {
    LittleFS.remove(tmp);
    return 0;
  }
  return len;
}

bool DtgPrefs::isKey(const char *key)
{
  if (!open_)
    return false;
  char path[48];
  if (!buildPath(key, path, sizeof(path)))
    return false;
  return LittleFS.exists(path);
}

bool DtgPrefs::clear()
{
  if (!open_ || readOnly_)
    return false;
  char dir[24];
  snprintf(dir, sizeof(dir), "/p/%s", ns_);
  Dir d = LittleFS.openDir(dir);
  while (d.next())
  {
    char path[48];
    snprintf(path, sizeof(path), "%s/%s", dir, d.fileName().c_str());
    LittleFS.remove(path);
  }
  return true;
}

bool DtgPrefs::remove(const char *key)
{
  if (!open_ || readOnly_)
    return false;
  char path[48];
  if (!buildPath(key, path, sizeof(path)))
    return false;
  if (!LittleFS.exists(path))
    return true;
  return LittleFS.remove(path);
}

size_t DtgPrefs::putUChar(const char *key, uint8_t value)
{
  return writeAll(key, &value, 1);
}

size_t DtgPrefs::putUInt(const char *key, uint32_t value)
{
  uint8_t buf[4];
  buf[0] = static_cast<uint8_t>(value);
  buf[1] = static_cast<uint8_t>(value >> 8);
  buf[2] = static_cast<uint8_t>(value >> 16);
  buf[3] = static_cast<uint8_t>(value >> 24);
  return writeAll(key, buf, 4);
}

size_t DtgPrefs::putBool(const char *key, bool value)
{
  return putUChar(key, value ? 1 : 0);
}

size_t DtgPrefs::putString(const char *key, const char *value)
{
  const char *v = value ? value : "";
  return writeAll(key, reinterpret_cast<const uint8_t *>(v), strlen(v));
}

size_t DtgPrefs::putBytes(const char *key, const void *value, size_t len)
{
  if (!value && len)
    return 0;
  return writeAll(key, static_cast<const uint8_t *>(value), len);
}

uint8_t DtgPrefs::getUChar(const char *key, uint8_t defaultValue)
{
  uint8_t v = 0;
  size_t n = 0;
  if (!readAll(key, &v, 1, &n) || n != 1)
    return defaultValue;
  return v;
}

uint16_t DtgPrefs::getUShort(const char *key, uint16_t defaultValue)
{
  uint8_t buf[2];
  size_t n = 0;
  if (!readAll(key, buf, 2, &n) || n != 2)
    return defaultValue;
  return static_cast<uint16_t>(buf[0] | (static_cast<uint16_t>(buf[1]) << 8));
}

uint32_t DtgPrefs::getUInt(const char *key, uint32_t defaultValue)
{
  uint8_t buf[4];
  size_t n = 0;
  if (!readAll(key, buf, 4, &n) || n != 4)
    return defaultValue;
  return static_cast<uint32_t>(buf[0]) | (static_cast<uint32_t>(buf[1]) << 8) |
         (static_cast<uint32_t>(buf[2]) << 16) | (static_cast<uint32_t>(buf[3]) << 24);
}

bool DtgPrefs::getBool(const char *key, bool defaultValue)
{
  return getUChar(key, defaultValue ? 1 : 0) != 0;
}

size_t DtgPrefs::getString(const char *key, char *value, size_t maxLen)
{
  if (!value || maxLen == 0)
    return 0;
  size_t n = 0;
  if (!readAll(key, reinterpret_cast<uint8_t *>(value), maxLen - 1, &n))
  {
    value[0] = '\0';
    return 0;
  }
  value[n] = '\0';
  return n;
}

String DtgPrefs::getString(const char *key, const String &defaultValue)
{
  char buf[128];
  const size_t n = getString(key, buf, sizeof(buf));
  if (n == 0 && !isKey(key))
    return defaultValue;
  return String(buf);
}

size_t DtgPrefs::getBytesLength(const char *key)
{
  if (!open_)
    return 0;
  char path[48];
  if (!buildPath(key, path, sizeof(path)))
    return 0;
  File f = LittleFS.open(path, "r");
  if (!f)
    return 0;
  const size_t n = f.size();
  f.close();
  return n;
}

size_t DtgPrefs::getBytes(const char *key, void *buf, size_t maxLen)
{
  size_t n = 0;
  if (!readAll(key, static_cast<uint8_t *>(buf), maxLen, &n))
    return 0;
  return n;
}

#endif // ESP8266
