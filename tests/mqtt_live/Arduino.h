#pragma once
#include <chrono>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <string>
#define HEX 16
inline uint32_t millis() {
  using namespace std::chrono;
  return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
class String {
 public:
  String(const char* value="") : value_(value) {}
  String(uint32_t number, int) { char text[16]; snprintf(text,sizeof(text),"%x",number); value_=text; }
  const char* c_str() const { return value_.c_str(); }
  size_t length() const { return value_.size(); }
  String operator+(const char* rhs) const { return String((value_+rhs).c_str()); }
  String operator+(const String& rhs) const { return String((value_+rhs.value_).c_str()); }
 private:
  std::string value_;
};
struct FakeEsp { uint32_t getChipId() const { return 123; } };
extern FakeEsp ESP;
