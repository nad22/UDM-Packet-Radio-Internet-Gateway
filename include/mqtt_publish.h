#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mqtt {

enum class PublishResult { Success, HeaderFailed, PayloadFailed, EndFailed };

template <typename Client, typename Topic>
PublishResult publishFrame(Client& client, const Topic& topic, const char* data, size_t length) {
  if (!client.beginMessage(topic, static_cast<unsigned long>(length))) {
    client.stop();
    return PublishResult::HeaderFailed;
  }
  if (client.write(reinterpret_cast<const uint8_t*>(data), length) != length) {
    client.stop();
    return PublishResult::PayloadFailed;
  }
  if (client.endMessage() != 1) {
    client.stop();
    return PublishResult::EndFailed;
  }
  return PublishResult::Success;
}

} // namespace mqtt
