#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include "IdvnProtocol.h"
#include "MqttCommand.h"
#include "MqttWire.h"

class MqttBridge {
 public:
  MqttBridge(idvn::Controller& controller, void (*service)()) : controller_(controller), service_(service) {}
  void begin();
  void tick();
  // Capture terminal results before HTTP is allowed to overwrite controller.job().
  void syncJob();
  bool connected() const { return phase_ == 3; }
 private:
  void close();
  bool send();
  bool publish(const char* suffix, const char* payload, bool retained);
  bool publishJson(const char* suffix, bool retained);
  void packet(const mqtt::Packet& packet);
  void command(const mqtt::Publish& publish);
  void result(uint32_t id, const char* error, const char* status = "rejected");
  void publishOperation();
  void publishState();
  idvn::Controller& controller_;
  void (*service_)();
  WiFiClient tcp_;
  IPAddress broker_;
  mqtt::Parser parser_;
  mqtt::Writer writer_;
  idvn::MqttCommandGate gate_;
  // 64-bit hosts use larger ArduinoJson slots than the ESP8266 target.
  StaticJsonDocument<sizeof(void*) == 8 ? 8192 : 4096> json_;
  char serialized_[2048] = {};
  char session_[17] = {}, operationSession_[17] = {};
  idvn::Job operation_;
  uint32_t requestId_ = 0;
  uint32_t attemptAt_ = 0, handshakeAt_ = 0, lastTx_ = 0, pingAt_ = 0, stateAt_ = 0;
  uint8_t phase_ = 0;
  bool enabled_ = false, pingPending_ = false, dirty_ = false, stateRequested_ = false;
};
