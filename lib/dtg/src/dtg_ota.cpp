/**
 * @file dtg_ota.cpp
 */
#include "dtg_config.h"
#if DTG_FEATURE_OTA

#include "dtg_ota.h"

#include "dtg_debug.h"
#include "dtg_heap_guard.h"
#include "dtg_modem_lifecycle.h"
#include "dtg_ota_boot.h"
#include "dtg_ota_hash.h"
#include "dtg_ota_policy.h"
#include "dtg_tls_roots.h"

#include "dtg_platform.h"

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#if defined(ESP8266)
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <Updater.h>
#include <bearssl/bearssl_hash.h>
#else
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>
#endif

#include <new>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace detail
{

namespace
{

static constexpr int kOtaHttpTimeoutMs = 30000;
static constexpr uint32_t kStreamIdleAbortMs = 60000;
static constexpr uint32_t kOtaHttpRetryDelayMs = 2000;
static constexpr uint32_t kOtaMqttReleaseSettleMs = 500;

static DtgOtaMqttResumeFn s_mqttResume = nullptr;

static void otaTrace(const char *fmt, ...)
{
  char msg[180];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  DTG_LOG("[OTA] %s", msg);
}

static void otaTraceErr(const char *ctx)
{
#if defined(ESP32)
  DTG_LOG("[OTA] %s err=%d %s", ctx ? ctx : "?", static_cast<int>(Update.getError()),
          Update.errorString());
#else
  DTG_LOG("[OTA] %s err=%d", ctx ? ctx : "?", static_cast<int>(Update.getError()));
#endif
}

static void otaUpdateAbort()
{
#if defined(ESP32)
  Update.abort();
#elif defined(ESP8266)
  // ESP8266 Updater has no abort(); end(false) discards the partial image.
  (void)Update.end(false);
#endif
}

struct DtgOtaSha256
{
#if defined(ESP32)
  mbedtls_sha256_context ctx{};
  void begin()
  {
    mbedtls_sha256_init(&ctx);
#if MBEDTLS_VERSION_MAJOR >= 3
    mbedtls_sha256_starts(&ctx, 0);
#else
    mbedtls_sha256_starts_ret(&ctx, 0);
#endif
  }
  void update(const uint8_t *data, size_t len)
  {
#if MBEDTLS_VERSION_MAJOR >= 3
    mbedtls_sha256_update(&ctx, data, len);
#else
    mbedtls_sha256_update_ret(&ctx, data, len);
#endif
  }
  void finish(uint8_t out[32])
  {
#if MBEDTLS_VERSION_MAJOR >= 3
    mbedtls_sha256_finish(&ctx, out);
#else
    mbedtls_sha256_finish_ret(&ctx, out);
#endif
    mbedtls_sha256_free(&ctx);
  }
  void reset()
  {
    mbedtls_sha256_free(&ctx);
    begin();
  }
#else
  br_sha256_context ctx{};
  void begin() { br_sha256_init(&ctx); }
  void update(const uint8_t *data, size_t len) { br_sha256_update(&ctx, data, len); }
  void finish(uint8_t out[32]) { br_sha256_out(&ctx, out); }
  void reset() { begin(); }
#endif
};

/** Log host/path only — strip ?query so download tokens never hit Serial. */
static void otaTraceUrlHint(const char *url)
{
  if (!url)
  {
    otaTrace("url=(null)");
    return;
  }
  char hint[96];
  size_t n = 0;
  while (url[n] && url[n] != '?' && n < sizeof(hint) - 1)
    ++n;
  memcpy(hint, url, n);
  hint[n] = '\0';
  if (strlen(url) > n)
  {
    if (n + 4 < sizeof(hint))
    {
      hint[n++] = '.';
      hint[n++] = '.';
      hint[n++] = '.';
      hint[n] = '\0';
    }
  }
  otaTrace("url_hint=%s", hint);
}

bool publishOtaStatus(PubSubClient &mqtt, const char *hubTopicPrefix, const char *jobId,
                      const char *status, int progress, const char *errorMsg)
{
  if (!hubTopicPrefix || !jobId)
  {
    otaTrace("publishOtaStatus skip: missing prefix or jobId");
    return false;
  }
  char t[128];
  snprintf(t, sizeof(t), "%sup/ota_status", hubTopicPrefix);
  JsonDocument doc;
  doc["jobId"] = jobId;
  doc["status"] = status;
  doc["progress"] = progress;
  if (errorMsg && errorMsg[0])
    doc["error"] = errorMsg;
  char buf[256];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  const bool ok =
      mqtt.publish(t, reinterpret_cast<const uint8_t *>(buf), static_cast<unsigned int>(n), false);
  otaTrace("up/ota_status publish %s topic=%s err=%s", ok ? "OK" : "FAIL", t,
           errorMsg && errorMsg[0] ? errorMsg : "-");
  return ok;
}

static bool resumeMqttIfNeeded(PubSubClient &mqtt)
{
  if (mqtt.connected())
    return true;
  if (!s_mqttResume)
  {
    otaTrace("MQTT resume: no handler registered");
    return false;
  }
  otaTrace("MQTT resume after HTTPS...");
  dtgWdtFeed();
  const bool ok = s_mqttResume();
  otaTrace("MQTT resume %s connected=%d heap=%u", ok ? "OK" : "FAIL", (int)mqtt.connected(),
           static_cast<unsigned int>(dtgFreeHeap()));
  return ok && mqtt.connected();
}

struct OtaHttpSession
{
  WiFiClient plain;
  WiFiClientSecure secure;
  HTTPClient http;
#if defined(ESP8266)
  BearSSL::X509List *trust_ = nullptr;
  char *caRam_ = nullptr;
#endif

  explicit OtaHttpSession(const char *caPem)
  {
#if DTG_OTA_TLS_INSECURE
    (void)caPem;
    secure.setInsecure();
#elif defined(ESP8266)
    const char *pem = (caPem && pgm_read_byte(caPem) != 0) ? caPem : DTG_TLS_CA_BUNDLE;
    const size_t len = strlen_P(pem);
    caRam_ = static_cast<char *>(malloc(len + 1));
    if (caRam_)
    {
      memcpy_P(caRam_, pem, len);
      caRam_[len] = '\0';
      trust_ = new (std::nothrow) BearSSL::X509List(caRam_);
      if (trust_)
        secure.setTrustAnchors(trust_);
    }
#else
    secure.setCACert(caPem && caPem[0] ? caPem : DTG_TLS_CA_BUNDLE);
#endif
    http.setTimeout(kOtaHttpTimeoutMs);
#if defined(ESP32)
    http.setConnectTimeout(kOtaHttpTimeoutMs);
#endif
  }

  ~OtaHttpSession()
  {
    http.end();
#if defined(ESP8266)
    delete trust_;
    free(caRam_);
#endif
  }

  bool beginUrl(const char *url)
  {
    const bool isHttps = strncmp(url, "https://", 8) == 0;
    return isHttps ? http.begin(secure, url) : http.begin(plain, url);
  }
};

/**
 * Download + flash with HTTP Range resume. Returns nullptr on success, or stable error code.
 * Must run with MQTT TLS already released on cellular.
 */
static const char *otaDownloadAndFlash(const char *firmwareUrl, size_t *writtenOut,
                                       const uint8_t *expectSha, uint32_t expectedSize,
                                       const char *caPem)
{
  DtgOtaSha256 sha;
  sha.begin();

  size_t written = 0;
  bool updateBegun = false;
  int lastLoggedProgress = -1;
  const size_t expectedBytes = expectedSize > 0 ? static_cast<size_t>(expectedSize) : 0;

  for (int attempt = 1; attempt <= DTG_OTA_RESUME_ATTEMPTS; ++attempt)
  {
    OtaHttpSession sess(caPem);
    if (!sess.beginUrl(firmwareUrl))
    {
      otaTrace("http.begin failed attempt=%d", attempt);
      if (attempt == DTG_OTA_RESUME_ATTEMPTS)
      {
        if (updateBegun)
          otaUpdateAbort();
        return "http_begin_failed";
      }
      delay(kOtaHttpRetryDelayMs);
      dtgWdtFeed();
      continue;
    }

    if (written > 0)
    {
      char range[48];
      snprintf(range, sizeof(range), "bytes=%u-", static_cast<unsigned>(written));
      sess.http.addHeader("Range", range);
      otaTrace("HTTP Range resume offset=%u attempt=%d", static_cast<unsigned>(written), attempt);
    }
    else
    {
      otaTrace("HTTP GET start attempt=%d/%d...", attempt, DTG_OTA_RESUME_ATTEMPTS);
    }

    const int code = sess.http.GET();
    otaTrace("HTTP GET code=%d", code);

    if (written > 0 && code == HTTP_CODE_OK)
    {
      // Server ignored Range — abort partial flash; restart from offset 0.
      otaTrace("Range ignored (200) — restart download");
      if (updateBegun)
        otaUpdateAbort();
      sha.reset();
      written = 0;
      updateBegun = false;
      lastLoggedProgress = -1;
      sess.http.end();
      delay(kOtaHttpRetryDelayMs);
      continue;
    }

    if (written == 0 && code != HTTP_CODE_OK)
    {
      sess.http.end();
      if (attempt == DTG_OTA_RESUME_ATTEMPTS)
      {
        static char errHttp[24];
        snprintf(errHttp, sizeof(errHttp), "http_%d", code);
        return errHttp;
      }
      delay(kOtaHttpRetryDelayMs);
      dtgWdtFeed();
      continue;
    }

    if (written > 0 && code != HTTP_CODE_PARTIAL_CONTENT && code != HTTP_CODE_OK)
    {
      sess.http.end();
      if (attempt == DTG_OTA_RESUME_ATTEMPTS)
      {
        if (updateBegun)
          otaUpdateAbort();
        static char errHttp[24];
        snprintf(errHttp, sizeof(errHttp), "http_%d", code);
        return errHttp;
      }
      delay(kOtaHttpRetryDelayMs);
      continue;
    }

    const int contentLen = sess.http.getSize();
    WiFiClient *stream = sess.http.getStreamPtr();
    if (!stream)
    {
      if (updateBegun)
        otaUpdateAbort();
      return "no_stream";
    }

    if (!updateBegun)
    {
#if defined(ESP8266)
      const size_t freeSketch = ESP.getFreeSketchSpace();
      size_t updateSize = expectedBytes > 0
                              ? expectedBytes
                              : (contentLen > 0 ? static_cast<size_t>(contentLen)
                                                : freeSketch);
      if (updateSize > freeSketch)
      {
        otaTrace("image larger than free sketch space");
        return "ota_no_space";
      }
#else
      const size_t updateSize = expectedBytes > 0
                                    ? expectedBytes
                                    : (contentLen > 0 ? static_cast<size_t>(contentLen)
                                                      : UPDATE_SIZE_UNKNOWN);
#endif
      otaTrace("Update.begin size=%u", static_cast<unsigned int>(updateSize));
      if (!Update.begin(updateSize))
      {
        otaTraceErr("Update.begin FAILED");
        return "ota_begin_failed";
      }
      updateBegun = true;
    }

    uint8_t buff[1024];
    uint32_t idleMs = 0;
    bool streamOk = true;

    while (sess.http.connected() && !(expectedBytes > 0 && written >= expectedBytes))
    {
      dtgWdtFeed();
      const size_t available = stream->available();
      if (available == 0)
      {
        delay(1);
        idleMs += 1;
        if (expectedBytes > 0 && written >= expectedBytes)
          break;
        if (idleMs >= kStreamIdleAbortMs)
        {
          otaTrace("stream idle timeout written=%u — will resume",
                   static_cast<unsigned int>(written));
          streamOk = false;
          break;
        }
        continue;
      }

      idleMs = 0;
      const int toRead = (available > sizeof(buff)) ? (int)sizeof(buff) : (int)available;
      const int readLen = stream->readBytes(reinterpret_cast<char *>(buff), toRead);
      if (readLen <= 0)
      {
        streamOk = false;
        break;
      }
      if (Update.write(buff, static_cast<size_t>(readLen)) != static_cast<size_t>(readLen))
      {
        otaTraceErr("Update.write");
        otaUpdateAbort();
        return "ota_write_failed";
      }
      sha.update(buff, static_cast<size_t>(readLen));
      written += static_cast<size_t>(readLen);
      if (expectedBytes == 0)
        continue;
      const int progress = static_cast<int>((written * 100UL) / expectedBytes);
      if (progress >= 100)
        break;
      if (progress % 10 == 0 && progress != lastLoggedProgress)
      {
        otaTrace("downloading ~%d%% written=%u", progress, static_cast<unsigned int>(written));
        lastLoggedProgress = progress;
      }
    }

    sess.http.end();

    if (expectedBytes > 0 && written >= expectedBytes)
      break;
    if (expectedBytes == 0 && streamOk && written > 0)
      break; // unknown size, stream ended cleanly

    if (attempt == DTG_OTA_RESUME_ATTEMPTS)
    {
      if (updateBegun)
        otaUpdateAbort();
      return "stream_idle_timeout";
    }
    delay(kOtaHttpRetryDelayMs);
    dtgWdtFeed();
  }

  uint8_t digest[32] = {};
  sha.finish(digest);

  if (expectedSize > 0 && written != static_cast<size_t>(expectedSize))
  {
    otaUpdateAbort();
    return "size_mismatch";
  }
  if (expectSha && !dtgOtaSha256Equal(digest, expectSha))
  {
    otaUpdateAbort();
    return "checksum_mismatch";
  }

  if (!Update.end())
  {
    otaTraceErr("Update.end() FAILED");
    return "ota_end_failed";
  }
  if (!Update.isFinished())
  {
    otaTraceErr("Update.isFinished()=false");
    return "ota_not_finished";
  }

  if (writtenOut)
    *writtenOut = written;
  return nullptr;
}

} // namespace

void dtgOtaSetMqttResumeHandler(DtgOtaMqttResumeFn fn) { s_mqttResume = fn; }

bool hubOtaPerformFromUrl(PubSubClient &mqtt, const char *hubTopicPrefix, const char *jobId,
                          const char *firmwareUrl, const char *checksumSpec,
                          uint32_t expectedSize, const char *caPem)
{
  struct OtaGuardScope
  {
    OtaGuardScope() { dtgHeapGuardSetOtaInProgress(true); }
    ~OtaGuardScope() { dtgHeapGuardSetOtaInProgress(false); }
  } otaGuard;

  struct MqttBufShrink
  {
    PubSubClient &mqtt;
    size_t restoreBytes;
    MqttBufShrink(PubSubClient &m) : mqtt(m), restoreBytes(DTG_MQTT_BUFFER_CORE)
    {
      mqtt.setBufferSize(2048);
    }
    ~MqttBufShrink() { mqtt.setBufferSize(restoreBytes); }
  } mqttBufShrink(mqtt);

  const char *jid = jobId && jobId[0] ? jobId : "";

  otaTrace("hubOtaPerformFromUrl enter jobId=%s mqtt_connected=%d free_heap=%u",
           jid[0] ? jid : "(null)", (int)mqtt.connected(),
           static_cast<unsigned int>(dtgFreeHeap()));
  otaTraceUrlHint(firmwareUrl);

  uint8_t expectSha[32] = {};
  const DtgOtaPolicyError pol = dtgOtaValidateStart(
      firmwareUrl, checksumSpec, DTG_OTA_ALLOW_HTTP != 0, expectSha);
  if (pol != DTG_OTA_POLICY_OK)
  {
    const char *err = dtgOtaPolicyErrorCode(pol);
    otaTrace("abort policy=%s", err);
    if (mqtt.connected())
      publishOtaStatus(mqtt, hubTopicPrefix, jid, "failed", 0, err);
    return false;
  }

  if (!mqtt.connected())
  {
    otaTrace("abort: MQTT not connected");
    return false;
  }

  // Status first while MQTT TLS is still up.
  publishOtaStatus(mqtt, hubTopicPrefix, jid, "downloading", 1, "");

  otaTrace("release MQTT TLS before HTTPS heap=%u",
           static_cast<unsigned int>(dtgFreeHeap()));
  mqtt.disconnect();
  delay(kOtaMqttReleaseSettleMs);
  dtgWdtFeed();

  size_t written = 0;
  const char *err =
      otaDownloadAndFlash(firmwareUrl, &written, expectSha, expectedSize, caPem);

  if (err)
  {
    (void)resumeMqttIfNeeded(mqtt);
    if (mqtt.connected())
      publishOtaStatus(mqtt, hubTopicPrefix, jid, "failed", 0, err);
    else
      otaTrace("fail %s (mqtt down; Hub stall/timeout)", err);
    return false;
  }

  uint32_t partAddr = 0;
#if defined(ESP32)
  const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
  partAddr = next ? next->address : 0;
#endif
  (void)dtgOtaBootSavePending(jid, partAddr);

  otaTrace("flash OK written=%u — resume MQTT for installing", static_cast<unsigned int>(written));
  if (resumeMqttIfNeeded(mqtt))
  {
    // success is published after reboot self-test (dtg_ota_boot).
    publishOtaStatus(mqtt, hubTopicPrefix, jid, "installing", 95, "");
    delay(800);
    mqtt.loop();
  }
  else
  {
    otaTrace("installing but MQTT resume failed — device updated; Hub may show stall until reboot");
  }

  otaTrace("reboot to pending-verify image");
  dtgWdtFeed();
  dtgInvokeModemPrepareForRestart();
  dtgWdtFeed();
  dtgRestart();
  return true;
}

} // namespace detail

#endif // DTG_FEATURE_OTA