#include "MqttBridge.h"
#include "settings.h"
extern "C" {
#include <user_interface.h>
}

namespace {
String topic(const char* suffix) { return String(IDVN_MQTT_PREFIX) + "/" + suffix; }
bool terminal(idvn::JobStatus status) {
  return status != idvn::JobStatus::Queued && status != idvn::JobStatus::Sent && status != idvn::JobStatus::None;
}
}
void MqttBridge::begin() {
  enabled_ = sizeof(IDVN_MQTT_IP) > 1 && broker_.fromString(IDVN_MQTT_IP);
  for (const char* p=IDVN_MQTT_PREFIX; *p; ++p)
    if (static_cast<uint8_t>(*p) < 33 || static_cast<uint8_t>(*p) > 126 || *p=='+' || *p=='#') enabled_=false;
  tcp_.setTimeout(100);
  attemptAt_=millis()-10000U;
}
void MqttBridge::close() {
  tcp_.abort(); phase_=0; pingPending_=false; parser_.reset(); attemptAt_=millis();
}
bool MqttBridge::send() {
  service_();
  if (!writer_.size() || tcp_.write(writer_.data(),writer_.size()) != writer_.size()) {
    close(); return false;
  }
  lastTx_=millis(); service_(); return true;
}
bool MqttBridge::publish(const char* suffix,const char* payload,bool retained) {
  return connected() && writer_.publish(topic(suffix).c_str(),payload,strlen(payload),retained) && send();
}
bool MqttBridge::publishJson(const char* suffix,bool retained) {
  if (json_.overflowed() || measureJson(json_) >= sizeof(serialized_)) return false;
  serializeJson(json_,serialized_,sizeof(serialized_));
  return publish(suffix,serialized_,retained);
}
void MqttBridge::result(uint32_t id,const char* error,const char* status) {
  json_.clear(); json_["session"]=session_; json_["id"]=id;
  json_["status"]=status; if (error) json_["error"]=error;
  publishJson("result",false);
}
void MqttBridge::syncJob() {
  if (!requestId_ || terminal(operation_.status)) return;
  const auto& current=controller_.job();
  if (current.id != operation_.id) {
    operation_.status=idvn::JobStatus::TransportError; dirty_=true;
  } else if (current.status != operation_.status || current.controllerError != operation_.controllerError) {
    operation_=current; dirty_=true;
  }
}
void MqttBridge::publishOperation() {
  json_.clear(); json_["session"]=operationSession_; json_["id"]=requestId_;
  json_["operation_id"]=operation_.id; json_["status"]=idvn::jobStatusName(operation_.status);
  if (operation_.status==idvn::JobStatus::Rejected) json_["controller_error"]=operation_.controllerError;
  if (publishJson("result",false)) dirty_=false;
}
void MqttBridge::publishState() {
  const uint32_t now=millis();
  json_.clear(); json_["session"]=session_; json_["uptime_ms"]=now;
  const bool online=controller_.online(now);
  json_["controller_online"]=online; json_["controller_confirmed"]=controller_.confirmed();
  if (controller_.hasState()) json_["state_age_ms"]=controller_.stateAge(now);
  if (online) idvn::stateJson(json_.createNestedObject("data"),controller_.state());
  else json_["data"]=nullptr;
  json_["temperature_c"]=nullptr; json_["left_relay_on"]=nullptr; json_["right_relay_on"]=nullptr;
  idvn::capabilitiesJson(json_.createNestedObject("capabilities"));
  if (publishJson("state",true)) { stateAt_=now; stateRequested_=false; }
}
void MqttBridge::command(const mqtt::Publish& message) {
  const String expected=topic("command");
  if (message.topicLength != expected.length() || memcmp(message.topic,expected.c_str(),message.topicLength)) return;
  if (message.retained) { result(0,"retained_command_rejected"); return; }
  if (message.length > 1024) { result(0,"request_too_large"); return; }
  StaticJsonDocument<2048> input;
  if (deserializeJson(input,message.payload,message.length,DeserializationOption::NestingLimit(5))) {
    result(0,"invalid_json"); return;
  }
  const auto request=gate_.decode(input.as<JsonVariantConst>(),false,session_);
  if (request.error) { result(request.id,request.error); return; }
  if (request.readOnly) { stateRequested_=true; result(request.id,nullptr,"accepted"); return; }
  service_(); syncJob();
  if (dirty_) publishOperation();
  if (!connected()) return;
  if (!controller_.online(millis()) || !controller_.confirmed()) {
    result(request.id,"controller_not_ready"); return;
  }
  if (!controller_.queue(request.opcode,request.bytes,request.length,millis())) {
    result(request.id,"operation_in_progress"); return;
  }
  requestId_=request.id; memcpy(operationSession_,session_,sizeof(session_));
  operation_=controller_.job(); dirty_=true;
  publishOperation(); stateRequested_=true;
}
void MqttBridge::packet(const mqtt::Packet& p) {
  if (phase_==1) {
    // Clean sessions must not report an existing session.
    if (p.header!=0x20 || p.length!=2 || p.data[0]!=0 || p.data[1]!=0) { close(); return; }
    phase_=2; handshakeAt_=millis();
    if (!writer_.subscribe(topic("command").c_str()) || !send()) close();
    return;
  }
  // MQTT permits delivery of matching retained publishes before SUBACK.
  // Ignore these before ready; retained commands are never executed.
  if (phase_==2 && p.header==0x90 && p.length==3 && p.data[0]==0 && p.data[1]==1 && p.data[2]<=1) {
    phase_=3; stateRequested_=true;
    if (requestId_) dirty_=true;
    publish("availability","online",true);
    return;
  }
  if ((p.header>>4)==3 && (phase_==2 || phase_==3)) {
    mqtt::Publish message;
    if (!mqtt::decodePublish(p,message)) { close(); return; }
    if (message.id) {
      const uint8_t ack[]={static_cast<uint8_t>(message.id>>8),static_cast<uint8_t>(message.id)};
      if (!writer_.control(0x40,ack,sizeof(ack)) || !send()) return;
    }
    if (phase_==3) command(message);
    return;
  }
  if (phase_==3 && p.header==0xd0 && p.length==0 && pingPending_) { pingPending_=false; return; }
  close();
}
void MqttBridge::tick() {
  syncJob();
  if (!enabled_) return;
  const uint32_t now=millis();
  if (WiFi.status()!=WL_CONNECTED) { if (phase_) close(); return; }
  if (!phase_) {
    if (static_cast<uint32_t>(now-attemptAt_)<10000U) return;
    attemptAt_=now; service_();
    if (!tcp_.connect(broker_,IDVN_MQTT_PORT)) { tcp_.abort(); return; }
    tcp_.setNoDelay(true); parser_.reset(); gate_.reset();
    snprintf(session_,sizeof(session_),"%08lx%08lx",static_cast<unsigned long>(os_random()),static_cast<unsigned long>(os_random()));
    const String id=String("hotrod-")+String(ESP.getChipId(),HEX);
    phase_=1; handshakeAt_=millis();
    if (!writer_.connect(id.c_str(),IDVN_MQTT_USER,IDVN_MQTT_PASSWORD,topic("availability").c_str()) || !send()) close();
    return;
  }
  if (!tcp_.connected() || parser_.expired(now) ||
      (phase_!=3 && static_cast<uint32_t>(now-handshakeAt_)>=3000U) ||
      (pingPending_ && static_cast<uint32_t>(now-pingAt_)>=5000U)) { close(); return; }
  // A slow broker cannot trap the main loop in readByte/wait-for-payload loops.
  for (size_t n=0; n<256 && tcp_.available(); ++n) {
    mqtt::Packet incoming;
    const int value=tcp_.read(); if (value<0) break;
    if (parser_.push(static_cast<uint8_t>(value),millis(),incoming)) {
      packet(incoming); break; // At most one complete packet per pass.
    }
    if (parser_.failed()) { close(); return; }
  }
  if (!connected()) return;
  if (dirty_) { publishOperation(); return; }
  if (stateRequested_ || static_cast<uint32_t>(millis()-stateAt_)>=15000U) { publishState(); return; }
  if (!pingPending_ && static_cast<uint32_t>(millis()-lastTx_)>=5000U) {
    if (writer_.control(0xc0,nullptr,0) && send()) { pingPending_=true; pingAt_=millis(); }
  }
}
