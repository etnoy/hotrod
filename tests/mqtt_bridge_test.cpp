#include "MqttBridge.h"
#include <assert.h>
#include <string>

uint32_t fakeNow=100;
FakeEsp ESP; FakeWifi WiFi;
std::deque<uint8_t> WiFiClient::input;
std::vector<std::vector<uint8_t>> WiFiClient::output;
bool WiFiClient::online=false, WiFiClient::connectOk=true, WiFiClient::writeOk=true;
uint32_t WiFiClient::timeout=0;
static std::vector<std::vector<uint8_t>> uart;
static bool sendUart(const uint8_t* bytes,size_t length,void*) {
  uart.emplace_back(bytes,bytes+length); return true;
}
static idvn::Controller controller(sendUart,nullptr);
static uint32_t serviced=0;
static void service() { ++serviced; controller.tick(fakeNow); }
static void reply(uint8_t opcode,const std::vector<uint8_t>& payload) {
  uint8_t bytes[idvn::kMaxFrame];
  const size_t count=idvn::encodeFrame(opcode,payload.data(),payload.size(),bytes,sizeof(bytes));
  for (size_t i=0;i<count;++i) controller.receive(bytes[i],fakeNow);
}
static void broker(const std::vector<uint8_t>& bytes) {
  WiFiClient::input.insert(WiFiClient::input.end(),bytes.begin(),bytes.end());
}
static void brokerPublish(const std::string& json,bool retained=false) {
  mqtt::Writer writer;
  assert(writer.publish("hotrod/idvn/command",json.c_str(),json.size(),retained));
  broker(std::vector<uint8_t>(writer.data(),writer.data()+writer.size()));
}
static std::string published(const char* suffix) {
  std::string result;
  for (const auto& bytes:WiFiClient::output) {
    mqtt::Parser parser; mqtt::Packet packet={0,nullptr,0};
    for (auto byte:bytes) parser.push(byte,fakeNow,packet);
    mqtt::Publish message;
    if (mqtt::decodePublish(packet,message) &&
        std::string(reinterpret_cast<const char*>(message.topic),message.topicLength)==std::string("hotrod/idvn/")+suffix)
      result=std::string(reinterpret_cast<const char*>(message.payload),message.length);
  }
  return result;
}
static std::string session() {
  StaticJsonDocument<8192> json;
  assert(!deserializeJson(json,published("state")));
  return json["session"].as<const char*>();
}
static std::string command(const std::string& session,uint32_t id,const char* name) {
  return "{\"session\":\""+session+"\",\"id\":"+std::to_string(id)+",\"command\":\""+name+"\"}";
}
int main() {
  controller.begin(fakeNow); service(); reply(idvn::Confirm,{});
  service(); reply(idvn::ReadAll,std::vector<uint8_t>(91,0));
  assert(controller.online(fakeNow));
  MqttBridge bridge(controller,service); bridge.begin(); bridge.tick();
  assert(WiFiClient::timeout==100 && !bridge.connected());
  assert(WiFiClient::output.front()[0]==0x10);
  broker({0x20,2,0,0}); bridge.tick();
  assert(WiFiClient::output.back()[0]==0x82);
  broker({0x90,3,0,1,1}); bridge.tick();
  assert(bridge.connected() && published("availability")=="online");
  const std::string firstSession=session();
  assert(published("state").find("\"temperature_c\":null")!=std::string::npos);
  size_t before=uart.size();
  brokerPublish(command(firstSession,1,"identify"),true); bridge.tick();
  assert(uart.size()==before && published("result").find("retained_command_rejected")!=std::string::npos);
  brokerPublish(command(firstSession,1,"start_now")); bridge.tick();
  assert(uart.size()==before && published("result").find("controller_extension_required")!=std::string::npos);
  brokerPublish(command(firstSession,2,"identify")); bridge.tick();
  assert(controller.job().command==idvn::Identify && controller.job().status==idvn::JobStatus::Sent);
  reply(idvn::Identify,{0}); bridge.syncJob(); bridge.tick();
  assert(published("result").find("applied")!=std::string::npos);
  before=uart.size();
  brokerPublish(command(firstSession,2,"identify")); bridge.tick();
  assert(uart.size()==before && published("result").find("duplicate_or_old_request")!=std::string::npos);
  // A concurrent HTTP write must not overwrite the completed MQTT result.
  assert(controller.queue(idvn::Identify,nullptr,0,fakeNow));
  service(); reply(idvn::ReadAll,std::vector<uint8_t>(91,0)); service(); reply(idvn::Identify,{0});
  bridge.syncJob();
  WiFiClient::online=false; bridge.tick();
  fakeNow+=10000; bridge.tick(); broker({0x20,2,0,0}); bridge.tick();
  broker({0x90,3,0,1,0}); bridge.tick(); bridge.tick();
  const std::string secondSession=session(); assert(secondSession!=firstSession);
  brokerPublish(command(firstSession,3,"identify")); bridge.tick();
  assert(published("result").find("invalid_session")!=std::string::npos);
  // Partial input is serviced incrementally and times out without waiting.
  const uint32_t calls=serviced;
  broker({0x30,100,0}); bridge.tick();
  fakeNow+=2000; bridge.tick(); assert(!bridge.connected() && serviced>=calls);
  // Handshake rejection, permission denial, lost broker and timeout are closed.
  fakeNow+=10000; bridge.tick(); broker({0x20,2,0,5}); bridge.tick(); assert(!WiFiClient::online);
  fakeNow+=10000; bridge.tick(); broker({0x20,2,0,0}); bridge.tick();
  broker({0x90,3,0,1,0x80}); bridge.tick(); assert(!WiFiClient::online);
  fakeNow+=10000; bridge.tick(); fakeNow+=3000; bridge.tick(); assert(!WiFiClient::online);
  puts("MQTT bridge handshake, state, UART acknowledgments, replay rejection, reconnect and broker failure tests passed");
}
