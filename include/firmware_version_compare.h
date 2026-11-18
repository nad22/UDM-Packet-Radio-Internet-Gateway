#pragma once

#include <stddef.h>

namespace firmware_version {

constexpr const char* skipZeros(const char* part) {
  return *part == '0' && part[1] >= '0' && part[1] <= '9'
      ? skipZeros(part + 1) : part;
}

constexpr size_t partLength(const char* part) {
  return *part == '\0' || *part == '.' ? 0 : 1 + partLength(part + 1);
}

constexpr int compareDigits(const char* left, const char* right, size_t length) {
  return length == 0 ? 0
      : *left != *right ? (*left < *right ? -1 : 1)
      : compareDigits(left + 1, right + 1, length - 1);
}

constexpr int compareParts(const char* left, const char* right) {
  return partLength(skipZeros(left)) != partLength(skipZeros(right))
      ? (partLength(skipZeros(left)) < partLength(skipZeros(right)) ? -1 : 1)
      : compareDigits(skipZeros(left), skipZeros(right), partLength(skipZeros(left)));
}

// Both inputs must already be validated as three numeric components.
constexpr int compare(const char* left, const char* right) {
  return compareParts(left, right) != 0 ? compareParts(left, right)
      : left[partLength(left)] == '\0' ? 0
      : compare(left + partLength(left) + 1, right + partLength(right) + 1);
}

}
