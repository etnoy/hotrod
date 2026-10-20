#pragma once
#include <stddef.h>
#include <stdint.h>

namespace garo {
constexpr size_t kMaxPayload = 128;
constexpr size_t kMaxFrame = kMaxPayload + 5;
constexpr uint32_t kFrameGapMs = 100;

enum Command : uint8_t {
  Confirm = 0x01, Activity = 0x11, Identify = 0x13, SetTime = 0x15,
  SetMode = 0x21, SetWorking = 0x23, SetDeparture = 0x25,
  SetReduction = 0x27, ReadAll = 0x28
};

struct Frame {
  uint8_t command = 0;
  uint8_t length = 0;
  uint8_t payload[kMaxPayload] = {};
};

uint16_t crc16(const uint8_t* bytes, size_t length, uint16_t initial = 0);
size_t encodeFrame(uint8_t command, const uint8_t* payload, size_t length,
                   uint8_t* output, size_t capacity);

class Parser {
 public:
  bool push(uint8_t byte, uint32_t now, Frame& output);
  void expire(uint32_t now);
  uint32_t crcErrors = 0, lengthErrors = 0, gapResets = 0;
 private:
  void discard(size_t count);
  uint8_t bytes_[kMaxFrame] = {};
  size_t used_ = 0;
  uint32_t lastByte_ = 0;
};

struct Mode { uint8_t left = 0, right = 0, timers = 1, period = 0; };
struct Working {
  uint8_t days = 127;
  uint8_t fromHours = 0, fromMinutes = 0, toHours = 23, toMinutes = 59;
  uint8_t fromMonth = 1, fromDay = 1, toMonth = 12, toDay = 31;
};
struct Clock {
  uint8_t year = 0, month = 1, day = 1, weekDay = 1;
  uint8_t hours = 0, minutes = 0, seconds = 0;
};
struct Point { int8_t temperature = 0; uint8_t minutes = 0; };
struct Curve { Point points[16]; uint8_t count = 0; };
struct State {
  Mode mode;
  Working working;
  Curve departure, reduction;
  Clock time;
  uint16_t hardwareVersion = 0, controllerVersion = 0, freeKiB = 0;
  uint8_t storageExponent = 0;
};

bool encodeMode(const Mode& value, uint8_t output[4]);
bool encodeWorking(const Working& value, uint8_t output[9]);
bool encodeClock(const Clock& value, uint8_t output[7]);
bool encodeCurve(const Curve& value, bool reduction, uint8_t output[32]);
bool decodeState(const uint8_t* payload, size_t length, State& output);
int responseLength(uint8_t command);
bool allowedRequest(uint8_t command, size_t length);

enum class JobStatus { None, Queued, Sent, Applied, Rejected, Timeout, TransportError };
struct Job {
  uint32_t id = 0;
  uint8_t command = 0, controllerError = 0;
  JobStatus status = JobStatus::None;
};
const char* jobStatusName(JobStatus value);

// This layer has no network or Arduino dependency. Call tick and receive often.
class Controller {
 public:
  using Sender = bool (*)(const uint8_t*, size_t, void*);
  Controller(Sender sender, void* context) : sender_(sender), context_(context) {}
  void begin(uint32_t now);
  void tick(uint32_t now);
  void receive(uint8_t byte, uint32_t now);
  bool queue(uint8_t command, const uint8_t* payload, size_t length, uint32_t now);
  bool online(uint32_t now) const;
  bool confirmed() const { return confirmed_; }
  bool hasState() const { return hasState_; }
  uint32_t stateAge(uint32_t now) const { return now - stateAt_; }
  const State& state() const { return state_; }
  const Job& job() const { return job_; }
  const Parser& parser() const { return parser_; }
  uint32_t timeouts = 0, receivedFrames = 0, ignoredFrames = 0, resetEvents = 0;
 private:
  bool send(uint8_t command, const uint8_t* payload, size_t length,
            uint32_t now, bool user);
  void accept(const Frame& frame, uint32_t now);
  Sender sender_;
  void* context_;
  Parser parser_;
  State state_;
  Job job_;
  uint8_t queuedPayload_[32] = {};
  uint8_t queuedLength_ = 0, pendingCommand_ = 0;
  bool pending_ = false, pendingUser_ = false, confirmed_ = false, hasState_ = false;
  bool quiet_ = false;
  uint32_t sentAt_ = 0, heartbeatAt_ = 0, pollAt_ = 0, stateAt_ = 0, quietAt_ = 0;
};
}  // namespace garo
