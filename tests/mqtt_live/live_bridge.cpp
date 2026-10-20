// Runs the real MqttBridge with a real TCP socket and a simulated controller.
#include "MqttBridge.h"
#include <thread>
FakeEsp ESP; FakeWifi WiFi;
static uint8_t pendingOpcode=0; static uint32_t writes=0;
static bool sendUart(const uint8_t* bytes,size_t,void*) {
  pendingOpcode=bytes[2]; if (bytes[2]!=idvn::Confirm && bytes[2]!=idvn::ReadAll) ++writes; return true;
}
static idvn::Controller controller(sendUart,nullptr);
static void service() {
  controller.tick(millis());
  if (!pendingOpcode) return;
  const uint8_t op=pendingOpcode; pendingOpcode=0;
  std::vector<uint8_t> payload;
  if (op==idvn::ReadAll) payload.assign(91,0); else if (op!=idvn::Confirm) payload.assign(1,0);
  uint8_t frame[idvn::kMaxFrame];
  const size_t n=idvn::encodeFrame(op,payload.data(),payload.size(),frame,sizeof(frame));
  for (size_t i=0;i<n;++i) controller.receive(frame[i],millis());
}
int main(int argc,char** argv) {
  const uint32_t seconds=argc>1?atoi(argv[1]):10;
  controller.begin(millis());
  MqttBridge bridge(controller,service); bridge.begin();
  const uint32_t end=millis()+seconds*1000;
  while (static_cast<int32_t>(end-millis())>0) {
    service(); bridge.tick();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  printf("controller_writes=%u connected=%d\n",writes,(int)bridge.connected());
}
