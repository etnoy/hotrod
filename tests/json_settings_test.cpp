#include "IdvnJson.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>

static bool parse(uint8_t command,const std::string& body,uint8_t* output,size_t& length) {
  StaticJsonDocument<4096> document;
  if (deserializeJson(document,body,DeserializationOption::NestingLimit(4))) return false;
  return idvn::encodeSettingsJson(command,document.as<JsonVariantConst>(),output,length);
}

int main() {
  uint8_t bytes[32]; size_t length=0;
  assert(parse(idvn::SetMode,"{\"left\":2,\"right\":0,\"period\":1,\"timers\":3}",bytes,length));
  const uint8_t mode[]={2,0,3,1}; assert(length==4 && !memcmp(bytes,mode,4));
  const char* invalid[]={
    "{}", "[]", "null", "{", "{\"left\":0,\"right\":1,\"timers\":1}",
    "{\"left\":true,\"right\":0,\"period\":0,\"timers\":1}",
    "{\"left\":1.9,\"right\":0,\"period\":0,\"timers\":1}",
    "{\"left\":\"1\",\"right\":0,\"period\":0,\"timers\":1}",
    "{\"left\":-1,\"right\":0,\"period\":0,\"timers\":1}",
    "{\"left\":256,\"right\":0,\"period\":0,\"timers\":1}",
    "{\"left\":0,\"right\":0,\"period\":0,\"timers\":0}",
    "{\"left\":4294967296,\"right\":0,\"period\":0,\"timers\":1}"
  };
  for (const auto body:invalid) assert(!parse(idvn::SetMode,body,bytes,length));
  assert(parse(idvn::SetTime,"{\"year\":26,\"month\":10,\"day\":25,\"weekDay\":7,\"hours\":8,\"minutes\":0,\"seconds\":0}",bytes,length));
  assert(length==7 && bytes[3]==7);
  assert(!parse(idvn::SetTime,"{\"year\":26,\"month\":10,\"day\":25,\"weekDay\":0,\"hours\":8,\"minutes\":0,\"seconds\":0}",bytes,length));
  assert(parse(idvn::SetDeparture,"{\"points\":[[-20,120],[0,60],[10,0]]}",bytes,length));
  assert(length==32 && bytes[0]==0xec && bytes[1]==120 && bytes[6]==0xff);
  assert(!parse(idvn::SetReduction,"{\"points\":[[-20,120],[0,60]]}",bytes,length));
  assert(!parse(idvn::SetDeparture,"{\"points\":[[0,60],[-20,120]]}",bytes,length));
  assert(!parse(idvn::SetDeparture,"{\"points\":[[-20,120],[0,60,0]]}",bytes,length));
  assert(!parse(idvn::SetDeparture,"{\"points\":[[-20,120]]}",bytes,length));
  assert(!parse(idvn::SetDeparture,"{\"points\":null}",bytes,length));
  const std::string working="{\"days\":127,\"fromTime\":{\"hours\":6,\"minutes\":10},"
      "\"toTime\":{\"hours\":7,\"minutes\":15},\"fromDate\":{\"month\":1,\"day\":1},\"toDate\":{\"month\":12,\"day\":31}}";
  assert(parse(idvn::SetWorking,working,bytes,length));
  const uint8_t window[]={127,6,10,7,15,1,1,12,31};
  assert(length==9 && !memcmp(bytes,window,9));
  assert(!parse(idvn::SetWorking,"{\"days\":0}",bytes,length));
  assert(!parse(0x30,"{}",bytes,length));
  puts("actual API JSON encoder rejects missing fields, booleans, floats, overflow, bad dates and malformed curves");
}
