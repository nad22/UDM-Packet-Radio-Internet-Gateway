#include "kiss_framing.h"
#include "kiss_hex.h"
#include "mqtt_publish.h"

#include <cassert>
#include <string>
#include <vector>

using Decoder = kiss::FrameDecoder<2048>;
using Bytes = std::vector<uint8_t>;

struct FakeClient {
  bool headerOk = true;
  bool endOk = true;
  bool partial = false;
  bool stopped = false;
  bool ended = false;
  unsigned long declaredSize = 0;
  Bytes sent;

  bool beginMessage(const std::string&, unsigned long size) {
    declaredSize = size;
    return headerOk;
  }
  size_t write(const uint8_t* data, size_t size) {
    size_t written = partial ? size - 1 : size;
    sent.assign(data, data + written);
    return written;
  }
  int endMessage() {
    ended = true;
    return endOk ? 1 : 0;
  }
  void stop() { stopped = true; }
};

int main() {
  Decoder decoder;
  std::vector<Bytes> frames;
  int overflows = 0;
  auto feed = [&](uint8_t byte) {
    Decoder::Result result = decoder.feed(byte);
    if (result == Decoder::Frame) {
      frames.emplace_back(decoder.data(), decoder.data() + decoder.size());
    } else if (result == Decoder::Overflow) {
      ++overflows;
    }
  };

  // Noise, empty delimiters, shared and doubled delimiters, NUL and KISS escapes.
  const Bytes stream = {0x42, 0xC0, 0xC0, 0x00, 0xDB, 0xDC, 0xDB, 0xDD, 0xC0,
                        0x00, 0x01, 0xC0, 0xC0, 0x00, 0x02, 0xC0};
  for (uint8_t byte : stream) feed(byte);
  assert((frames == std::vector<Bytes>{{0xC0, 0x00, 0xDB, 0xDC, 0xDB, 0xDD, 0xC0},
                                       {0xC0, 0x00, 0x01, 0xC0},
                                       {0xC0, 0x00, 0x02, 0xC0}}));
  assert(kiss::isSingleFrame(frames[0].data(), frames[0].size()));
  const Bytes joined = {0xC0, 0x00, 0xC0, 0x00, 0xC0};
  assert(!kiss::isSingleFrame(joined.data(), joined.size()));
  assert(!kiss::isSingleFrame(joined.data(), 2));
  assert(kiss::readHexByte("db") == 0xDB);
  for (unsigned int value = 0; value <= 255; ++value) {
    char hex[3];
    kiss::writeHexByte(static_cast<uint8_t>(value), hex);
    assert(kiss::readHexByte(hex) == value);
    // The XOR step is symmetric for each byte, including NUL and KISS escapes.
    kiss::writeHexByte(static_cast<uint8_t>(value ^ 0xA5), hex);
    assert((kiss::readHexByte(hex) ^ 0xA5) == value);
  }

  // Exactly 2048 bytes succeeds; 2049 is discarded up to the next FEND.
  for (size_t i = 0; i < 2046; ++i) feed(0x55);
  feed(0xC0);
  assert(frames.size() == 4 && frames.back().size() == 2048);
  assert(frames.back().front() == 0xC0 && frames.back().back() == 0xC0);
  for (size_t i = 0; i < 2047; ++i) feed(0x55);
  assert(overflows == 1);
  feed(0x66);
  feed(0xC0);
  feed(0x00);
  feed(0xC0);
  assert(frames.size() == 5 && (frames.back() == Bytes{0xC0, 0x00, 0xC0}));
  feed(0x00);
  assert(frames.size() == 5); // No publication before the terminating FEND.

  Bytes longFrame(2048, 0x55);
  longFrame[0] = 0xC0;
  longFrame[1] = 0x00;
  longFrame[2] = 0xDB;
  longFrame[3] = 0xDC;
  longFrame[4] = 0xDB;
  longFrame[5] = 0xDD;
  longFrame.back() = 0xC0;
  const std::string secret = "test-key";
  for (bool encrypted : {false, true}) {
    std::string hex;
    for (size_t i = 0; i < longFrame.size(); ++i) {
      char encoded[3];
      uint8_t byte = longFrame[i];
      if (encrypted) byte ^= static_cast<uint8_t>(secret[i % secret.size()]);
      kiss::writeHexByte(byte, encoded);
      hex += encoded;
    }
    Bytes decoded;
    for (size_t i = 0; i < hex.size(); i += 2) {
      uint8_t byte = kiss::readHexByte(hex.data() + i);
      if (encrypted) byte ^= static_cast<uint8_t>(secret[(i / 2) % secret.size()]);
      decoded.push_back(byte);
    }
    assert(decoded == longFrame);
  }

  // The actual streaming helper declares and writes the full JSON in one PUBLISH.
  std::string hex;
  for (uint8_t byte : longFrame) {
    char encoded[3];
    kiss::writeHexByte(byte, encoded);
    hex += encoded;
  }
  std::string payload = "{\"payload_hex\":\"" + hex + "\",\"payload_length\":2048}";
  FakeClient client;
  assert(mqtt::publishFrame(client, std::string("topic"), payload.data(), payload.size()) ==
         mqtt::PublishResult::Success);
  assert(client.declaredSize == payload.size() && client.ended && !client.stopped);
  assert(std::string(client.sent.begin(), client.sent.end()) == payload);

  FakeClient headerFail;
  headerFail.headerOk = false;
  assert(mqtt::publishFrame(headerFail, std::string("topic"), payload.data(), payload.size()) ==
         mqtt::PublishResult::HeaderFailed);
  assert(headerFail.stopped && !headerFail.ended && headerFail.sent.empty());

  FakeClient partial;
  partial.partial = true;
  assert(mqtt::publishFrame(partial, std::string("topic"), payload.data(), payload.size()) ==
         mqtt::PublishResult::PayloadFailed);
  assert(partial.stopped && !partial.ended);

  FakeClient endFail;
  endFail.endOk = false;
  assert(mqtt::publishFrame(endFail, std::string("topic"), payload.data(), payload.size()) ==
         mqtt::PublishResult::EndFailed);
  assert(endFail.stopped && endFail.ended);
}
