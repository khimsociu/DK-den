/**
 * Host self-check for dtg_ota_policy.h
 * g++ -std=c++17 -I../src -o ota_policy_test ota_policy_test.cpp
 */
#include <cassert>
#include <cstdio>
#include <cstring>

#include "../src/dtg_ota_policy.h"

int main()
{
  uint8_t sha[32] = {};
  const char *emptySha =
      "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

  assert(dtgOtaValidateStart(nullptr, emptySha, false, sha) ==
         DTG_OTA_POLICY_MISSING_URL);
  assert(dtgOtaValidateStart("", emptySha, false, sha) == DTG_OTA_POLICY_MISSING_URL);

  assert(dtgOtaValidateStart("http://evil/fw.bin", emptySha, false, sha) ==
         DTG_OTA_POLICY_HTTP_NOT_ALLOWED);
  assert(dtgOtaValidateStart("ftp://evil/fw.bin", emptySha, true, sha) ==
         DTG_OTA_POLICY_HTTP_NOT_ALLOWED);
  assert(dtgOtaValidateStart("http://lab/fw.bin", emptySha, true, sha) ==
         DTG_OTA_POLICY_OK);

  assert(dtgOtaValidateStart("https://cdn.example/fw.bin", nullptr, false, sha) ==
         DTG_OTA_POLICY_BAD_CHECKSUM);
  assert(dtgOtaValidateStart("https://cdn.example/fw.bin", "", false, sha) ==
         DTG_OTA_POLICY_BAD_CHECKSUM);
  assert(dtgOtaValidateStart("https://cdn.example/fw.bin", "deadbeef", false, sha) ==
         DTG_OTA_POLICY_BAD_CHECKSUM);

  assert(dtgOtaValidateStart("https://cdn.example/fw.bin", emptySha, false, sha) ==
         DTG_OTA_POLICY_OK);
  assert(sha[0] == 0xe3);

  assert(strcmp(dtgOtaPolicyErrorCode(DTG_OTA_POLICY_HTTP_NOT_ALLOWED),
                "http_not_allowed") == 0);

  std::puts("ota_policy_test OK");
  return 0;
}
