#pragma once
#include <ArduinoJson.h>
#include "IdvnProtocol.h"

namespace idvn {
bool encodeSettingsJson(uint8_t command, JsonVariantConst json, uint8_t* output, size_t& length);
}
