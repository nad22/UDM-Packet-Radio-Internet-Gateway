#pragma once

#include <stdint.h>

namespace kiss {

// Input must already be validated as hexadecimal.
inline uint8_t hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return c - 'a' + 10;
}

inline uint8_t readHexByte(const char* hex) {
  return (hexNibble(hex[0]) << 4) | hexNibble(hex[1]);
}

inline void writeHexByte(uint8_t value, char* output) {
  const char digits[] = "0123456789ABCDEF";
  output[0] = digits[value >> 4];
  output[1] = digits[value & 0x0F];
  output[2] = '\0';
}

} // namespace kiss
