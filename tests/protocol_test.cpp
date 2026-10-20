#include "IdvnProtocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>

using namespace idvn;
using Bytes=std::vector<uint8_t>;

static Bytes frame(uint8_t command, const Bytes& payload={}) {
  Bytes out(kMaxFrame);
  const size_t length=encodeFrame(command,payload.data(),payload.size(),out.data(),out.size());
  assert(length); out.resize(length); return out;
}

static bool feed(Parser& parser, const Bytes& bytes, Frame& out, uint32_t now) {
  bool found=false;
  for (const auto byte:bytes) if (parser.push(byte,now,out)) found=true;
  return found;
}

static void testFrames() {
  const uint8_t text[]={'1','2','3','4','5','6','7','8','9'};
  assert(crc16(text,sizeof(text))==0x31c3);
  assert(frame(Confirm)==Bytes({0,4,1,0x21,0x10}));
  assert(frame(ReadAll)==Bytes({0,4,0x28,0x6a,0xa5}));
  assert(frame(Identify)==Bytes({0,4,0x13,0x52,0x22}));
  uint8_t small[4];
  assert(encodeFrame(Confirm,nullptr,0,small,sizeof(small))==0);
  assert(encodeFrame(SetMode,nullptr,4,small,sizeof(small))==0);
  for (size_t size=0;size<=kMaxPayload;++size) {
    Bytes payload(size);
    for (size_t i=0;i<size;++i) payload[i]=static_cast<uint8_t>(i*37);
    const Bytes encoded=frame(0x28,payload);
    Parser parser; Frame decoded;
    assert(feed(parser,encoded,decoded,10));
    assert(decoded.command==0x28 && decoded.length==size);
    assert(!size || memcmp(decoded.payload,payload.data(),size)==0);
    // Bit corruption in the CRC must not deliver a frame.
    Bytes corrupt=encoded; corrupt.back()^=0x80;
    Parser damaged;
    assert(!feed(damaged,corrupt,decoded,20));
    damaged.expire(200);
    assert(feed(damaged,frame(Confirm),decoded,201));
  }
  Parser fragmented; Frame out;
  auto bytes=frame(SetMode,{0,1,3,2});
  for (size_t i=0;i<bytes.size();++i)
    assert(fragmented.push(bytes[i],static_cast<uint32_t>(i+10),out)==(i+1==bytes.size()));
  assert(out.command==SetMode && out.payload[0]==0 && out.payload[3]==2);
  assert(feed(fragmented,Bytes({0xfe,0x7f,0x80,0,1}),out,100)==false);
  assert(feed(fragmented,frame(Confirm),out,101));
  Parser partial;
  assert(!feed(partial,Bytes({0,132,0x28,0,0}),out,0xfffffff0U));
  assert(feed(partial,frame(Confirm),out,200));  // Gap and millis wrap-around.
  assert(partial.gapResets==1);
  Bytes tooLong(kMaxPayload+1); uint8_t output[kMaxFrame];
  assert(!encodeFrame(0x28,tooLong.data(),tooLong.size(),output,sizeof(output)));
}

static Bytes statePayload() {
  Bytes state(91,0xff);
  const uint8_t prefix[]={2,1,3,2,127,6,10,7,15,1,1,12,31};
  memcpy(state.data(),prefix,sizeof(prefix));
  state[13]=0xec; state[14]=120; state[15]=0; state[16]=60;
  state[45]=0xec; state[46]=40; state[47]=0; state[48]=20;
  const uint8_t tail[]={26,10,25,7,6,30,5,104,0,100,0,22,0x34,0x12};
  memcpy(state.data()+77,tail,sizeof(tail));
  return state;
}

static void testCodecs() {
  uint8_t out[32];
  Mode mode; mode.left=2; mode.right=1; mode.timers=3; mode.period=2;
  assert(encodeMode(mode,out));
  assert(memcmp(out,Bytes({2,1,3,2}).data(),4)==0);
  mode.timers=0; assert(!encodeMode(mode,out));
  Working work; work.fromMonth=4; work.fromDay=31;
  assert(!encodeWorking(work,out));
  work.fromMonth=2; work.fromDay=29; assert(encodeWorking(work,out));
  work.fromHours=24; assert(!encodeWorking(work,out));
  Clock time; time.year=26; time.month=10; time.day=25; time.weekDay=7;
  assert(encodeClock(time,out));
  time.weekDay=0; assert(!encodeClock(time,out));
  time.weekDay=7; time.month=2; time.day=29; assert(!encodeClock(time,out));
  time.year=24; assert(encodeClock(time,out));
  Curve curve; curve.count=2; curve.points[0].temperature=-20; curve.points[0].minutes=120;
  curve.points[1].temperature=0; curve.points[1].minutes=60;
  assert(encodeCurve(curve,false,out));
  assert(out[0]==0xec && out[1]==120 && out[2]==0 && out[3]==60);
  assert(out[4]==0xff && out[31]==0xff);
  assert(!encodeCurve(curve,true,out));
  curve.points[0].minutes=40; assert(encodeCurve(curve,true,out));
  curve.points[1].temperature=-20; assert(!encodeCurve(curve,true,out));
  State state; const auto raw=statePayload();
  assert(!decodeState(raw.data(),90,state));
  assert(decodeState(raw.data(),raw.size(),state));
  assert(state.mode.timers==3 && state.mode.period==2 && state.working.fromHours==6);
  assert(state.departure.count==2 && state.departure.points[0].temperature==-20);
  assert(state.reduction.count==2 && state.reduction.points[1].minutes==20);
  assert(state.time.weekDay==7 && state.time.seconds==5);
  assert(state.hardwareVersion==104 && state.controllerVersion==100);
  assert(state.storageExponent==22 && state.freeKiB==0x1234);
  for (uint8_t cmd=0x30;cmd<=0x32;++cmd) assert(!allowedRequest(cmd,0));
  assert(!allowedRequest(0x29,77));  // No bulk overwrite or firmware transfer surface.
}

struct FakeTransport {
  std::vector<Bytes> sent;
  bool fail=false;
  static bool send(const uint8_t* bytes,size_t size,void* context) {
    auto& self=*static_cast<FakeTransport*>(context);
    if (self.fail) return false;
    self.sent.emplace_back(bytes,bytes+size); return true;
  }
  uint8_t lastCommand() const { assert(!sent.empty()); return sent.back()[2]; }
};

static void reply(Controller& controller,uint8_t command,const Bytes& payload,uint32_t now) {
  for (const auto byte:frame(command,payload)) controller.receive(byte,now);
}

static void bringOnline(Controller& controller,FakeTransport& io,uint32_t now) {
  controller.begin(now); controller.tick(now);
  assert(io.lastCommand()==Confirm);
  reply(controller,Confirm,{},now+1);
  controller.tick(now+2); assert(io.lastCommand()==ReadAll);
  reply(controller,ReadAll,statePayload(),now+3);
  assert(controller.online(now+4) && controller.confirmed());
}

static void testController() {
  FakeTransport io; Controller controller(FakeTransport::send,&io);
  bringOnline(controller,io,1000);
  uint8_t mode[]={2,0,1,0};
  assert(controller.queue(SetMode,mode,4,1010));
  assert(!controller.queue(Identify,nullptr,0,1010));
  controller.tick(1010); assert(io.lastCommand()==SetMode);
  reply(controller,Identify,{0},1011);  // Unrelated reply must not complete the operation.
  assert(controller.job().status==JobStatus::Sent);
  reply(controller,SetMode,{0,0},1012);  // Wrong response length.
  assert(controller.job().status==JobStatus::Sent);
  reply(controller,SetMode,{0},1013);
  assert(controller.job().status==JobStatus::Applied);
  controller.tick(1014); assert(io.lastCommand()==ReadAll);
  reply(controller,ReadAll,statePayload(),1015);
  assert(controller.queue(SetMode,mode,4,1020)); controller.tick(1020);
  reply(controller,SetMode,{2},1021);
  assert(controller.job().status==JobStatus::Rejected && controller.job().controllerError==2);
  assert(controller.queue(SetMode,mode,4,1030)); controller.tick(1030);
  const size_t sentBeforeTimeout=io.sent.size();
  controller.tick(1730); assert(controller.job().status==JobStatus::Timeout);
  reply(controller,SetMode,{0},1731);
  assert(controller.job().status==JobStatus::Timeout);  // No success after a lost-ACK timeout.
  controller.tick(2000); assert(io.sent.size()==sentBeforeTimeout);
  controller.tick(2730); assert(io.sent.size()==sentBeforeTimeout); // No mutating retry.
  controller.tick(11000); assert(io.lastCommand()==Confirm);
  reply(controller,Confirm,{},11001);
  assert(controller.queue(Identify,nullptr,0,11002));
  io.fail=true; controller.tick(11002);
  assert(controller.job().status==JobStatus::TransportError);
  io.fail=false;
  reply(controller,0x05,{},11003);
  assert(!controller.online(11004) && !controller.confirmed());
  controller.tick(11004); assert(io.lastCommand()==Confirm);
  reply(controller,Confirm,{},11005);
  assert(!controller.queue(Identify,nullptr,0,11006)); // Needs state after reset.
  controller.tick(17000); assert(io.lastCommand()==ReadAll);
  reply(controller,ReadAll,statePayload(),17001);
  assert(!controller.online(62001));
  assert(!controller.queue(Identify,nullptr,0,62001));
  // Network-independent servicing and deadlines work across millis overflow.
  FakeTransport wrappedIO; Controller wrapped(FakeTransport::send,&wrappedIO);
  bringOnline(wrapped,wrappedIO,0xfffffff0U);
  wrapped.tick(10000); assert(wrappedIO.lastCommand()==Confirm);
}

int main() {
  testFrames(); puts("frame vectors, payload boundaries, fragmentation, noise, CRC and timeout recovery passed");
  testCodecs(); puts("settings validation, signed curves, Sunday date and 91-byte state layout passed");
  testController(); puts("controller lifecycle, request matching, rejection, lost ACK, reset and wrap-around passed");
}
