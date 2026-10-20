#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include "IdvnProtocol.h"
#include "IdvnJson.h"
#include "settings.h"
#include "TinyHttpServer.h"
#include "MqttBridge.h"

namespace {
constexpr const char* kVersion = "0.2.0-dev";
void dispatchHttp();
TinyHttpServer server(80,dispatchHttp);
StaticJsonDocument<4096> response;
uint32_t lastWifiAttempt = 0;

bool sendSerial(const uint8_t* bytes, size_t length, void*) {
  return Serial.write(bytes, length) == length;
}
idvn::Controller controller(sendSerial, nullptr);

void serviceController() {
  const uint32_t now = millis();
  // Bound a pass even if the RX line is noisy; come back on the next loop.
  for (size_t n = 0; n < 512 && Serial.available(); ++n)
    controller.receive(static_cast<uint8_t>(Serial.read()), now);
  controller.tick(now);
}
MqttBridge mqttBridge(controller,serviceController);

void sendResponse(int status) {
  if (response.overflowed()) {
    server.send(500, "application/json", "{\"error\":\"response_capacity\"}");
    return;
  }
  String body;
  serializeJson(response, body);
  server.sendHeader("Cache-Control", "no-store");
  server.send(status, "application/json", body);
}

void error(int status, const char* message) {
  response.clear(); response["error"] = message; sendResponse(status);
}

bool authenticated() {
  if (sizeof(IDVN_API_TOKEN) == 1) {
    error(503, "api_token_not_configured"); return false;
  }
  const String supplied = server.header("Authorization");
  const String expected = String("Bearer ") + IDVN_API_TOKEN;
  if (supplied.length() != expected.length()) {
    error(401, "unauthorized"); return false;
  }
  uint8_t difference = 0;
  for (size_t i = 0; i < expected.length(); ++i) difference |= supplied[i] ^ expected[i];
  if (difference) { error(401, "unauthorized"); return false; }
  return true;
}

void jobJson(JsonObject object) {
  const auto& job = controller.job();
  object["id"] = job.id;
  object["command"] = job.command;
  object["status"] = idvn::jobStatusName(job.status);
  if (job.status == idvn::JobStatus::Rejected) object["controller_error"] = job.controllerError;
}

void health() {
  response.clear();
  response["firmware"] = "idvn-bridge"; response["version"] = kVersion;
  response["uptime_ms"] = millis();
  response["wifi_connected"] = WiFi.status() == WL_CONNECTED;
  response["controller_online"] = controller.online(millis());
  response["controller_confirmed"] = controller.confirmed();
  response["mqtt_connected"] = mqttBridge.connected();
  response["configured"] = sizeof(IDVN_WIFI_SSID) > 1 && sizeof(IDVN_API_TOKEN) > 1;
  sendResponse(200);
}

void status() {
  if (!authenticated()) return;
  response.clear();
  response["version"] = kVersion; response["uptime_ms"] = millis();
  response["ip"] = WiFi.localIP().toString();
  response["rssi"] = WiFi.RSSI(); response["free_heap"] = ESP.getFreeHeap();
  response["controller_online"] = controller.online(millis());
  response["controller_confirmed"] = controller.confirmed();
  if (controller.hasState()) response["state_age_ms"] = controller.stateAge(millis());
  auto serial = response.createNestedObject("serial");
  serial["frames"] = controller.receivedFrames; serial["ignored_frames"] = controller.ignoredFrames;
  serial["timeouts"] = controller.timeouts; serial["crc_errors"] = controller.parser().crcErrors;
  serial["length_errors"] = controller.parser().lengthErrors;
  serial["gap_resets"] = controller.parser().gapResets; serial["reset_events"] = controller.resetEvents;
  jobJson(response.createNestedObject("operation")); sendResponse(200);
}

void data() {
  if (!authenticated()) return;
  if (!controller.online(millis())) { error(503, "controller_state_unavailable_or_stale"); return; }
  idvn::stateJson(response.to<JsonObject>(), controller.state());
  response["state_age_ms"] = controller.stateAge(millis());
  response["version"]["bridge"] = kVersion;
  sendResponse(200);
}

void submit(uint8_t command) {
  if (!authenticated()) return;
  uint8_t bytes[32]; size_t length=0;
  if (command != idvn::Identify) {
    if (server.header("Content-Type") != "application/json" &&
        !server.header("Content-Type").startsWith("application/json;")) {
      error(415,"application_json_required"); return;
    }
    const String body=server.arg("plain");
    if (body.length()>1024) { error(413,"request_too_large"); return; }
    StaticJsonDocument<1536> document;
    const auto result=deserializeJson(document,body,DeserializationOption::NestingLimit(4));
    if (result || !idvn::encodeSettingsJson(command,document.as<JsonVariantConst>(),bytes,length)) {
      error(400,"invalid_or_incomplete_settings"); return;
    }
  }
  serviceController();
  mqttBridge.syncJob();
  if (!controller.online(millis()) || !controller.confirmed()) {
    error(503,"controller_not_ready"); return;
  }
  if (!controller.queue(command,bytes,length,millis())) { error(409,"operation_in_progress"); return; }
  response.clear(); jobJson(response.createNestedObject("operation"));
  server.sendHeader("Location",String("/v1/operation?id=")+controller.job().id);
  sendResponse(202);
}

void operation() {
  if (!authenticated()) return;
  if (server.hasArg("id")) {
    const String id=server.arg("id");
    if (!id.length() || id.length()>10) { error(400,"invalid_operation_id"); return; }
    for (size_t i=0;i<id.length();++i)
      if (id[i]<'0' || id[i]>'9') { error(400,"invalid_operation_id"); return; }
    const uint64_t number=strtoull(id.c_str(),nullptr,10);
    if (!number || number>UINT32_MAX) { error(400,"invalid_operation_id"); return; }
    if (number!=controller.job().id) { error(410,"operation_no_longer_available"); return; }
  }
  response.clear(); jobJson(response.createNestedObject("operation")); sendResponse(200);
}

void dispatchHttp() {
  const bool get=!strcmp(server.method(),"GET");
  const char* path=server.path();
  if (get && !strcmp(path,"/health")) health();
  else if (get && !strcmp(path,"/v1/status")) status();
  else if (get && !strcmp(path,"/v1/data")) data();
  else if (get && !strcmp(path,"/v1/operation")) operation();
  else if (!get && !strcmp(path,"/v1/mode")) submit(idvn::SetMode);
  else if (!get && !strcmp(path,"/v1/working")) submit(idvn::SetWorking);
  else if (!get && !strcmp(path,"/v1/time")) submit(idvn::SetTime);
  else if (!get && !strcmp(path,"/v1/departure")) submit(idvn::SetDeparture);
  else if (!get && !strcmp(path,"/v1/reduction")) submit(idvn::SetReduction);
  else if (!get && !strcmp(path,"/v1/identify")) submit(idvn::Identify);
  else error(404,"not_found");
}

void connectWifi(uint32_t now) {
  lastWifiAttempt=now;
  if (sizeof(IDVN_WIFI_SSID)>1) WiFi.begin(IDVN_WIFI_SSID,IDVN_WIFI_PASSWORD);
}
}  // namespace

void setup() {
  // Set output latches before enabling drivers. Matches the stock idle levels.
  digitalWrite(5,IDVN_IDLE_GPIO5?HIGH:LOW); pinMode(5,OUTPUT);
  digitalWrite(4,IDVN_IDLE_GPIO4?HIGH:LOW); pinMode(4,OUTPUT);
  Serial.setRxBufferSize(512);
  Serial.begin(115200,SERIAL_8N1);
  Serial.setDebugOutput(false);
  // UART0 belongs exclusively to the controller. Never print logs to Serial.
  controller.begin(millis()); serviceController();
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(IDVN_HOSTNAME);
  WiFi.setAutoReconnect(true);
  if (sizeof(IDVN_WIFI_SSID)==1) WiFi.disconnect();
  connectWifi(millis()); server.begin();
  mqttBridge.begin();
}

void loop() {
  serviceController();
  mqttBridge.tick();
  serviceController();
  server.handleClient();
  serviceController();
  const uint32_t now=millis();
  if (WiFi.status()!=WL_CONNECTED && static_cast<uint32_t>(now-lastWifiAttempt)>=30000U)
    connectWifi(now);
  yield();
}
