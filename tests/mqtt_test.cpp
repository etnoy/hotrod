#include "MqttWire.h"
#include "MqttCommand.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

static mqtt::Packet parse(mqtt::Parser& parser, const uint8_t* data, size_t size, uint32_t now=0) {
  mqtt::Packet packet={0,nullptr,0};
  for (size_t i=0;i<size;++i) {
    const bool complete=parser.push(data[i],now,packet);
    assert(complete==(i+1==size));
  }
  assert(!parser.failed()); return packet;
}
static idvn::MqttCommand request(idvn::MqttCommandGate& gate,const char* json,bool retained=false) {
  StaticJsonDocument<2048> document;
  assert(!deserializeJson(document,json));
  return gate.decode(document.as<JsonVariantConst>(),retained,"session");
}
int main() {
  mqtt::Writer writer; mqtt::Parser parser;
  assert(writer.connect("unit","user","password","unit/availability"));
  auto packet=parse(parser,writer.data(),writer.size());
  assert(packet.header==0x10 && packet.length>10);
  assert(!memcmp(packet.data,"\0\4MQTT\4\xe6\0\17",10));
  assert(writer.connect("unit","","","unit/availability"));
  packet=parse(parser,writer.data(),writer.size()); assert(packet.data[7]==0x26);
  assert(writer.subscribe("unit/command"));
  packet=parse(parser,writer.data(),writer.size());
  assert(packet.header==0x82 && packet.data[0]==0 && packet.data[1]==1 && packet.data[packet.length-1]==1);
  for (size_t size: {size_t(0),size_t(1),size_t(127),size_t(128),size_t(1024),size_t(1900)}) {
    const std::string body(size,'x');
    assert(writer.publish("unit/state",body.c_str(),body.size(),true));
    packet=parse(parser,writer.data(),writer.size());
    mqtt::Publish publish; assert(mqtt::decodePublish(packet,publish));
    assert(publish.retained && publish.length==size && !memcmp(publish.payload,body.data(),size));
  }
  assert(!writer.publish("unit/state",std::string(2048,'x').c_str(),2048,false));
  const uint8_t qos1[]={0x3a,8,0,1,'c',0,42,'{','}','!'};
  packet=parse(parser,qos1,sizeof(qos1));
  mqtt::Publish publish; assert(mqtt::decodePublish(packet,publish));
  assert(publish.id==42 && publish.length==3 && !publish.retained);
  const uint8_t invalidPublish[][6]={{0x34,4,0,1,'c',1},{0x30,4,0,2,'+','c'},
      {0x30,4,0,2,0,'c'},{0x32,4,0,1,'c',0},{0x38,4,0,1,'c',1}};
  for (const auto& bytes:invalidPublish) {
    packet=parse(parser,bytes,sizeof(bytes)); assert(!mqtt::decodePublish(packet,publish));
  }
  const uint8_t oversized[]={0x30,0x81,0x10};
  parser.reset(); for (auto byte:oversized) parser.push(byte,0,packet);
  assert(parser.failed());
  const uint8_t badLength[]={0x30,0x80,0x80,0x80,0x80};
  parser.reset(); for (auto byte:badLength) parser.push(byte,0,packet);
  assert(parser.failed());
  parser.reset(); parser.push(0x30,UINT32_MAX-999,packet);
  assert(!parser.expired(500) && parser.expired(1000));
  assert(!parser.push(0,1000,packet) && parser.failed());
  // Total deadline, not a per-byte deadline: slow trickles cannot extend it.
  parser.reset(); parser.push(0x30,0,packet); parser.push(8,1000,packet);
  parser.push(0,1999,packet); parser.push(0,2000,packet); assert(parser.failed());

  idvn::MqttCommandGate gate;
  auto command=request(gate,"{\"session\":\"session\",\"id\":1,\"command\":\"identify\"}",true);
  assert(!strcmp(command.error,"retained_command_rejected"));
  command=request(gate,"{\"session\":\"old\",\"id\":1,\"command\":\"identify\"}");
  assert(!strcmp(command.error,"invalid_session"));
  command=request(gate,"{\"session\":\"session\",\"id\":1,\"command\":\"identify\"}");
  assert(!command.error && command.opcode==idvn::Identify && !command.length);
  command=request(gate,"{\"session\":\"session\",\"id\":1,\"command\":\"identify\"}");
  assert(!strcmp(command.error,"duplicate_or_old_request"));
  command=request(gate,"{\"session\":\"session\",\"id\":2,\"command\":\"mode\",\"value\":{\"left\":2,\"right\":0,\"period\":1,\"timers\":3}}");
  assert(!command.error && command.length==4 && command.bytes[0]==2 && command.bytes[2]==3);
  command=request(gate,"{\"session\":\"session\",\"id\":3,\"command\":\"mode\",\"value\":{\"left\":2}}");
  assert(!strcmp(command.error,"invalid_or_incomplete_settings"));
  uint32_t id=4;
  for (const auto name:{"departure_time","start_now","stop","temperature"}) {
    std::string json="{\"session\":\"session\",\"id\":"+std::to_string(id++)+",\"command\":\""+name+"\"}";
    command=request(gate,json.c_str()); assert(!strcmp(command.error,"controller_extension_required"));
  }
  command=request(gate,"{\"session\":\"session\",\"id\":9,\"command\":\"data\"}");
  assert(!command.error && command.readOnly);
  for (const auto number:{"0","-1","true","1.2","4294967296","\"10\""}) {
    std::string json="{\"session\":\"session\",\"id\":"+std::string(number)+",\"command\":\"data\"}";
    command=request(gate,json.c_str()); assert(!strcmp(command.error,"invalid_request_id"));
  }
  gate.reset();
  command=request(gate,"{\"session\":\"session\",\"id\":1,\"command\":\"upload\"}");
  assert(!strcmp(command.error,"unknown_command"));
  // 64-bit host JsonVariant slots are larger than ESP8266's 32-bit slots.
  StaticJsonDocument<8192> state;
  idvn::State value; value.mode.left=2; value.departure.count=16; value.reduction.count=16;
  for (uint8_t i=0;i<16;++i) {
    value.departure.points[i].temperature=-35+i; value.departure.points[i].minutes=210;
    value.reduction.points[i].temperature=-35+i; value.reduction.points[i].minutes=60;
  }
  idvn::stateJson(state.createNestedObject("data"),value);
  idvn::capabilitiesJson(state.createNestedObject("capabilities"));
  state["temperature_c"]=nullptr;
  assert(!state.overflowed() && state["data"]["mode"]["left"]==2);
  assert(!state["capabilities"]["departure_time"].as<bool>() && state["temperature_c"].isNull());
  char serialized[2048]; assert(measureJson(state)<sizeof(serialized));
  const size_t length=serializeJson(state,serialized,sizeof(serialized));
  assert(writer.publish("hotrod/idvn/state",serialized,length,true));
  // Deterministic noise exercises bounded decoder transitions under sanitizers.
  uint32_t seed=1;
  for (uint32_t n=0;n<100000;++n) {
    seed=seed*1664525U+1013904223U;
    if (parser.failed() || n%97==0) parser.reset();
    parser.push(static_cast<uint8_t>(seed>>24),n,packet);
  }
  puts("MQTT framing, limits, deadlines, retained rejection, duplicate/session protection, settings and capabilities passed");
}
