#pragma once

#include <stddef.h>
#include <stdint.h>

namespace detail {

bool decodeModbusRegisters(const uint16_t *regs, size_t count, const char *transformer,
                           float scale, float offset, float &valueOut);

bool encodeModbusRegisterValue(float value, const char *transformer, float scale, float offset,
                               uint16_t &rawOut);

// Decode packed coil/discrete-input bits (FC01/FC02) into a numeric value.
// For length==1 this yields 0/1; for length>1 the first `bitCount` bits form an
// unsigned integer (LSB first), then scale/offset are applied.
bool decodeModbusBits(const uint8_t *bytes, size_t byteCount, uint16_t bitCount, float scale,
                      float offset, float &valueOut);

// True when a slave answer carries fewer values than the request asked for.
// Coil and discrete answers are measured in bits (8 per byte); register answers
// in 16-bit words. A short answer must be rejected, not decoded as valid data.
inline bool modbusResponseIsShort(bool isBits, uint16_t payloadBytes, uint16_t requestedQuantity) {
  if (requestedQuantity == 0)
    return false;
  const uint32_t got = isBits ? (uint32_t)payloadBytes * 8u : (uint32_t)payloadBytes / 2u;
  return got < requestedQuantity;
}

} // namespace detail
