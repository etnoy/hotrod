#pragma once
#include "IdvnJson.h"

namespace idvn {
struct MqttCommand {
  uint32_t id = 0;
  uint8_t opcode = 0;
  uint8_t bytes[32] = {};
  size_t length = 0;
  bool readOnly = false;
  const char* error = nullptr;
};
// One automation writer per connection; sequence numbers must increase.
// Every valid envelope consumes its ID, including rejected/busy commands.
class MqttCommandGate {
 public:
  void reset() { lastId_ = 0; }
  MqttCommand decode(JsonVariantConst json, bool retained, const char* session);
 private:
  uint32_t lastId_ = 0;
};
void stateJson(JsonObject object, const State& state);
void capabilitiesJson(JsonObject object);
}  // namespace idvn
