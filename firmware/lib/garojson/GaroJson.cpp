#include "GaroJson.h"

namespace garo {
bool integer(JsonVariantConst value, int minimum, int maximum, int& result) {
  if (!value.is<int>()) return false;
  const int parsed = value.as<int>();
  if (parsed < minimum || parsed > maximum) return false;
  result = parsed; return true;
}

bool byte(JsonVariantConst value, int minimum, int maximum, uint8_t& result) {
  int parsed;
  if (!integer(value, minimum, maximum, parsed)) return false;
  result = static_cast<uint8_t>(parsed); return true;
}

bool encodeSettingsJson(uint8_t command, JsonVariantConst json, uint8_t* bytes, size_t& length) {
  if (!json.is<JsonObjectConst>()) return false;
  switch (command) {
    case garo::SetMode: {
      garo::Mode v; length=4;
      return byte(json["left"],0,2,v.left) && byte(json["right"],0,2,v.right) &&
          byte(json["timers"],1,3,v.timers) && byte(json["period"],0,2,v.period) && garo::encodeMode(v,bytes);
    }
    case garo::SetTime: {
      garo::Clock v; length=7;
      return byte(json["year"],0,99,v.year) && byte(json["month"],1,12,v.month) &&
          byte(json["day"],1,31,v.day) && byte(json["weekDay"],1,7,v.weekDay) &&
          byte(json["hours"],0,23,v.hours) && byte(json["minutes"],0,59,v.minutes) &&
          byte(json["seconds"],0,59,v.seconds) && garo::encodeClock(v,bytes);
    }
    case garo::SetWorking: {
      garo::Working v; length=9;
      return byte(json["days"],0,127,v.days) &&
          byte(json["fromTime"]["hours"],0,23,v.fromHours) && byte(json["fromTime"]["minutes"],0,59,v.fromMinutes) &&
          byte(json["toTime"]["hours"],0,23,v.toHours) && byte(json["toTime"]["minutes"],0,59,v.toMinutes) &&
          byte(json["fromDate"]["month"],1,12,v.fromMonth) && byte(json["fromDate"]["day"],1,31,v.fromDay) &&
          byte(json["toDate"]["month"],1,12,v.toMonth) && byte(json["toDate"]["day"],1,31,v.toDay) &&
          garo::encodeWorking(v,bytes);
    }
    case garo::SetDeparture: case garo::SetReduction: {
      if (!json["points"].is<JsonArrayConst>()) return false;
      const auto points = json["points"].as<JsonArrayConst>();
      if (points.size() < 2 || points.size() > 16) return false;
      garo::Curve v; v.count=static_cast<uint8_t>(points.size()); length=32;
      for (uint8_t i=0;i<v.count;++i) {
        int temperature;
        if (!points[i].is<JsonArrayConst>() || points[i].size()!=2 ||
            !integer(points[i][0],-35,15,temperature) ||
            !byte(points[i][1],0,command==garo::SetReduction?60:210,v.points[i].minutes)) return false;
        v.points[i].temperature=static_cast<int8_t>(temperature);
      }
      return garo::encodeCurve(v,command==garo::SetReduction,bytes);
    }
    default: return false;
  }
}

}  // namespace garo
