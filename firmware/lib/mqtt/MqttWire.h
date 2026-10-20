#pragma once
#include <stddef.h>
#include <stdint.h>

// Bounded MQTT 3.1.1 subset. The caller owns TCP and feeds available bytes only.
// No socket reads, waits, allocation, or retry loops occur in this layer.
namespace mqtt {
constexpr size_t kCapacity = 2048;
struct Packet { uint8_t header; const uint8_t* data; size_t length; };
class Parser {
 public:
  bool push(uint8_t byte, uint32_t now, Packet& packet);
  bool expired(uint32_t now) const;
  bool failed() const { return failed_; }
  void reset();
 private:
  uint8_t bytes_[kCapacity] = {}, header_ = 0, lengthBytes_ = 0;
  size_t expected_ = 0, used_ = 0, multiplier_ = 1;
  bool started_ = false, body_ = false, failed_ = false, complete_ = false;
  uint32_t startedAt_ = 0;
};
class Writer {
 public:
  bool connect(const char* id, const char* user, const char* password, const char* willTopic);
  bool subscribe(const char* topic);
  bool publish(const char* topic, const char* payload, size_t length, bool retained);
  bool control(uint8_t header, const uint8_t* payload, size_t length);
  const uint8_t* data() const { return bytes_ + start_; }
  size_t size() const { return size_; }
 private:
  bool byte(uint8_t value);
  bool word(size_t value);
  bool string(const char* value);
  bool append(const void* bytes, size_t length);
  bool finish(uint8_t header);
  void begin();
  uint8_t bytes_[kCapacity + 5] = {};
  size_t used_ = 5, start_ = 0, size_ = 0;
};
struct Publish {
  const uint8_t* topic = nullptr;
  size_t topicLength = 0;
  const uint8_t* payload = nullptr;
  size_t length = 0;
  uint16_t id = 0;
  bool retained = false;
};
bool decodePublish(const Packet& packet, Publish& publish);
}  // namespace mqtt
