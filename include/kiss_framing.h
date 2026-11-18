#pragma once

#include <stddef.h>
#include <stdint.h>

namespace kiss {

constexpr uint8_t FEND = 0xC0;

inline bool isSingleFrame(const uint8_t* data, size_t length) {
  if (length < 3 || data[0] != FEND || data[length - 1] != FEND) return false;
  for (size_t i = 1; i + 1 < length; ++i) {
    if (data[i] == FEND) return false;
  }
  return true;
}

template <size_t Capacity>
class FrameDecoder {
 public:
  enum Result { None, Frame, Overflow };
  static_assert(Capacity >= 3, "KISS frame buffer must include a command and delimiters");

  Result feed(uint8_t byte) {
    if (frameReady_) {
      frameReady_ = false;
      length_ = 1;
      buffer_[0] = FEND;
    }

    if (byte == FEND) {
      if (started_ && !discarding_ && length_ > 1) {
        buffer_[length_++] = FEND;
        frameReady_ = true;
        return Frame;
      }
      started_ = true;
      discarding_ = false;
      length_ = 1;
      buffer_[0] = FEND;
    } else if (started_ && !discarding_) {
      if (length_ >= Capacity - 1) {
        discarding_ = true;
        length_ = 0;
        return Overflow;
      }
      buffer_[length_++] = byte;
    }
    return None;
  }

  const uint8_t* data() const { return buffer_; }
  size_t size() const { return length_; }

 private:
  uint8_t buffer_[Capacity] = {};
  size_t length_ = 0;
  bool started_ = false;
  bool discarding_ = false;
  bool frameReady_ = false;
};

} // namespace kiss
