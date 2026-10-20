#include "MqttCommand.h"
#include <string.h>

namespace idvn {
MqttCommand MqttCommandGate::decode(JsonVariantConst json, bool retained, const char* session) {
  MqttCommand out;
  if (retained) { out.error = "retained_command_rejected"; return out; }
  if (!json.is<JsonObjectConst>() || !json["session"].is<const char*>() ||
      strcmp(json["session"].as<const char*>(), session)) {
    out.error = "invalid_session"; return out;
  }
  if (!json["id"].is<uint32_t>() || !(out.id = json["id"].as<uint32_t>())) {
    out.error = "invalid_request_id"; return out;
  }
  if (out.id <= lastId_) { out.error = "duplicate_or_old_request"; return out; }
  lastId_ = out.id;
  const char* name = json["command"].as<const char*>();
  if (!name) { out.error = "invalid_command"; return out; }
  if (!strcmp(name,"data") || !strcmp(name,"status")) { out.readOnly = true; return out; }
  if (!strcmp(name,"departure_time") || !strcmp(name,"start_now") ||
      !strcmp(name,"stop") || !strcmp(name,"temperature")) {
    out.error = "controller_extension_required"; return out;
  }
  if (!strcmp(name,"mode")) out.opcode = SetMode;
  else if (!strcmp(name,"working")) out.opcode = SetWorking;
  else if (!strcmp(name,"time")) out.opcode = SetTime;
  else if (!strcmp(name,"departure_curve")) out.opcode = SetDeparture;
  else if (!strcmp(name,"reduction_curve")) out.opcode = SetReduction;
  else if (!strcmp(name,"identify")) { out.opcode = Identify; return out; }
  else { out.error = "unknown_command"; return out; }
  if (!encodeSettingsJson(out.opcode,json["value"],out.bytes,out.length))
    out.error = "invalid_or_incomplete_settings";
  return out;
}

static void curveJson(JsonObject object, const Curve& curve) {
  auto points = object.createNestedArray("points");
  for (uint8_t i=0; i<curve.count; ++i) {
    auto point = points.createNestedArray();
    point.add(static_cast<int>(curve.points[i].temperature)); point.add(curve.points[i].minutes);
  }
}
void stateJson(JsonObject object, const State& s) {
  auto mode = object.createNestedObject("mode");
  mode["left"]=s.mode.left; mode["right"]=s.mode.right;
  mode["timers"]=s.mode.timers; mode["period"]=s.mode.period;
  auto working=object.createNestedObject("working"); working["days"]=s.working.days;
  auto fromTime=working.createNestedObject("fromTime");
  fromTime["hours"]=s.working.fromHours; fromTime["minutes"]=s.working.fromMinutes;
  auto toTime=working.createNestedObject("toTime");
  toTime["hours"]=s.working.toHours; toTime["minutes"]=s.working.toMinutes;
  auto fromDate=working.createNestedObject("fromDate");
  fromDate["month"]=s.working.fromMonth; fromDate["day"]=s.working.fromDay;
  auto toDate=working.createNestedObject("toDate");
  toDate["month"]=s.working.toMonth; toDate["day"]=s.working.toDay;
  curveJson(object.createNestedObject("departure"),s.departure);
  curveJson(object.createNestedObject("reduction"),s.reduction);
  auto time=object.createNestedObject("time");
  time["year"]=s.time.year; time["month"]=s.time.month; time["day"]=s.time.day;
  time["weekDay"]=s.time.weekDay; time["hours"]=s.time.hours;
  time["minutes"]=s.time.minutes; time["seconds"]=s.time.seconds;
  auto version=object.createNestedObject("version");
  version["hardware"]=s.hardwareVersion; version["controller"]=s.controllerVersion;
  auto storage=object.createNestedObject("controller_storage");
  storage["capacity_exponent"]=s.storageExponent; storage["free_kib"]=s.freeKiB;
}
void capabilitiesJson(JsonObject object) {
  object["mode"]=true; object["working"]=true; object["time"]=true;
  object["departure_curve"]=true; object["reduction_curve"]=true; object["identify"]=true;
  object["departure_time"]=false; object["start_now"]=false; object["stop"]=false;
  object["temperature"]=false; object["relay_state"]=false;
}
}  // namespace idvn
