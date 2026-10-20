#include "MqttWire.h"
#include <string.h>

namespace mqtt {
void Parser::reset() {
  expected_ = used_ = 0; multiplier_ = 1; lengthBytes_ = 0;
  started_ = body_ = failed_ = complete_ = false;
}
bool Parser::expired(uint32_t now) const {
  return started_ && !complete_ && static_cast<uint32_t>(now - startedAt_) >= 2000U;
}
bool Parser::push(uint8_t byte, uint32_t now, Packet& packet) {
  if (failed_) return false;
  if (complete_) reset();
  if (expired(now)) { failed_ = true; return false; }
  if (!started_) {
    started_ = true; startedAt_ = now; header_ = byte;
    return false;
  }
  if (!body_) {
    ++lengthBytes_;
    expected_ += (byte & 127) * multiplier_;
    if (expected_ > kCapacity || (lengthBytes_ == 4 && (byte & 128))) {
      failed_ = true; return false;
    }
    multiplier_ *= 128;
    if (byte & 128) return false;
    if (lengthBytes_ > 1 && byte == 0) { failed_ = true; return false; }
    body_ = true;
  } else {
    bytes_[used_++] = byte;
  }
  if (body_ && used_ == expected_) {
    packet = {header_, bytes_, used_}; complete_ = true; return true;
  }
  return false;
}
void Writer::begin() { used_ = 5; size_ = 0; }
bool Writer::append(const void* bytes, size_t length) {
  if (length > sizeof(bytes_) - used_ || (length && !bytes)) return false;
  if (length) memcpy(bytes_ + used_, bytes, length);
  used_ += length; return true;
}
bool Writer::byte(uint8_t value) { return append(&value, 1); }
bool Writer::word(size_t value) {
  return value <= 65535 && byte(static_cast<uint8_t>(value >> 8)) && byte(static_cast<uint8_t>(value));
}
bool Writer::string(const char* value) {
  return value && word(strlen(value)) && append(value, strlen(value));
}
bool Writer::finish(uint8_t header) {
  size_t length = used_ - 5;
  uint8_t encoded[4]; size_t count = 0;
  do {
    encoded[count] = length % 128; length /= 128;
    if (length) encoded[count] |= 128;
    ++count;
  } while (length);
  start_ = 4 - count;
  bytes_[start_] = header;
  memcpy(bytes_ + start_ + 1, encoded, count);
  size_ = used_ - start_; return true;
}
bool Writer::connect(const char* id, const char* user, const char* password, const char* willTopic) {
  begin();
  const bool credentials = user && *user;
  // Clean session, retained QoS0 will; username/password are paired.
  if (!string("MQTT") || !byte(4) || !byte(credentials ? 0xe6 : 0x26) ||
      !word(15) || !string(id) || !string(willTopic) || !string("offline") ||
      (credentials && (!string(user) || !string(password)))) return false;
  return finish(0x10);
}
bool Writer::subscribe(const char* topic) {
  begin();
  return word(1) && string(topic) && byte(1) && finish(0x82);
}
bool Writer::publish(const char* topic, const char* payload, size_t length, bool retained) {
  begin();
  return string(topic) && append(payload, length) && finish(retained ? 0x31 : 0x30);
}
bool Writer::control(uint8_t header, const uint8_t* payload, size_t length) {
  begin(); return append(payload, length) && finish(header);
}
bool decodePublish(const Packet& p, Publish& out) {
  if ((p.header >> 4) != 3 || p.length < 2) return false;
  const uint8_t qos = (p.header >> 1) & 3;
  if (qos > 1 || (!qos && (p.header & 8))) return false;
  const size_t length = static_cast<size_t>(p.data[0]) * 256 + p.data[1];
  if (!length || length > p.length - 2) return false;
  for (size_t i = 0; i < length; ++i)
    if (!p.data[2+i] || p.data[2+i] == '+' || p.data[2+i] == '#') return false;
  size_t offset = 2 + length;
  out.id = 0;
  if (qos) {
    if (p.length - offset < 2) return false;
    out.id = static_cast<uint16_t>(p.data[offset] * 256 + p.data[offset+1]); offset += 2;
    if (!out.id) return false;
  }
  out.topic = p.data + 2; out.topicLength = length;
  out.payload = p.data + offset; out.length = p.length - offset;
  out.retained = p.header & 1;
  return true;
}
}  // namespace mqtt
