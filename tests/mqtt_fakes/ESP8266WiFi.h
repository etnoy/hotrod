#pragma once
#include "Arduino.h"
#include <deque>
#include <vector>
class IPAddress { public: bool fromString(const char* value) { return !strcmp(value,"192.0.2.1"); } };
constexpr int WL_CONNECTED=3;
struct FakeWifi { int state=WL_CONNECTED; int status() const { return state; } };
extern FakeWifi WiFi;
class WiFiClient {
 public:
  static std::deque<uint8_t> input;
  static std::vector<std::vector<uint8_t>> output;
  static bool online, connectOk, writeOk;
  static uint32_t timeout;
  void setTimeout(uint32_t value) { timeout=value; }
  void setNoDelay(bool) {}
  bool connect(IPAddress,uint16_t) { online=connectOk; return online; }
  void abort() { online=false; input.clear(); }
  bool connected() const { return online; }
  size_t write(const uint8_t* bytes,size_t length) {
    if (!writeOk) return 0;
    output.emplace_back(bytes,bytes+length); return length;
  }
  int available() const { return input.size(); }
  int read() { if (input.empty()) return -1; const auto byte=input.front(); input.pop_front(); return byte; }
};
