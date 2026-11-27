/**
 * @file dtg_ota_policy.h
 * @brief Host-testable FOTA URL/checksum gate (Arduino-free).
 *
 * Fail-closed: reject non-HTTPS (unless allowHttp) and missing/invalid SHA-256.
 */
#pragma once

#include <stddef.h>
#include <string.h>

#include "dtg_ota_hash.h"

enum DtgOtaPolicyError : int
{
  DTG_OTA_POLICY_OK = 0,
  DTG_OTA_POLICY_MISSING_URL = 1,
  DTG_OTA_POLICY_HTTP_NOT_ALLOWED = 2,
  DTG_OTA_POLICY_BAD_CHECKSUM = 3,
};

inline const char *dtgOtaPolicyErrorCode(DtgOtaPolicyError e)
{
  switch (e)
  {
  case DTG_OTA_POLICY_OK:
    return "";
  case DTG_OTA_POLICY_MISSING_URL:
    return "missing_url";
  case DTG_OTA_POLICY_HTTP_NOT_ALLOWED:
    return "http_not_allowed";
  case DTG_OTA_POLICY_BAD_CHECKSUM:
    return "bad_checksum";
  }
  return "policy_error";
}

/**
 * @param allowHttp  lab only (DTG_OTA_ALLOW_HTTP). Production must pass false.
 * @param outSha     optional; filled when checksum parses (32 bytes).
 */
inline DtgOtaPolicyError dtgOtaValidateStart(const char *firmwareUrl,
                                             const char *checksumSpec, bool allowHttp,
                                             uint8_t outSha[32])
{
  if (!firmwareUrl || !firmwareUrl[0])
    return DTG_OTA_POLICY_MISSING_URL;

  const bool isHttps = strncmp(firmwareUrl, "https://", 8) == 0;
  const bool isHttp = strncmp(firmwareUrl, "http://", 7) == 0;
  if (!isHttps)
  {
    if (!(allowHttp && isHttp))
      return DTG_OTA_POLICY_HTTP_NOT_ALLOWED;
  }

  uint8_t tmp[32] = {};
  uint8_t *dst = outSha ? outSha : tmp;
  if (!dtgOtaParseSha256Hex(checksumSpec, dst))
    return DTG_OTA_POLICY_BAD_CHECKSUM;

  return DTG_OTA_POLICY_OK;
}
