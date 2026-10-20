#pragma once
#include <ArduinoJson.h>
#include "GaroProtocol.h"

namespace garo {
bool encodeSettingsJson(uint8_t command, JsonVariantConst json, uint8_t* output, size_t& length);
}
