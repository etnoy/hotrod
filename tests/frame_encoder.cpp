// Host-only adapter for comparing the portable encoder with emulated stock code.
#include "GaroProtocol.h"
#include <iostream>
#include <string>
#include <stdio.h>
#include <stdlib.h>

int main() {
  std::string input;
  while (std::cin >> input) {
    if (input.size()<2 || input.size()%2 || input.size()>2*(garo::kMaxPayload+1)) return 1;
    uint8_t payload[garo::kMaxPayload+1];
    for (size_t i=0;i<input.size()/2;++i)
      payload[i]=static_cast<uint8_t>(strtoul(input.substr(2*i,2).c_str(),nullptr,16));
    uint8_t output[garo::kMaxFrame];
    const size_t count=garo::encodeFrame(payload[0],payload+1,input.size()/2-1,output,sizeof(output));
    if (!count) return 2;
    for (size_t i=0;i<count;++i) printf("%02x",output[i]);
    puts("");
  }
}
