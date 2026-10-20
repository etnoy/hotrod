#include "GaroProtocol.h"
#include <string.h>

namespace garo {
uint16_t crc16(const uint8_t* bytes, size_t length, uint16_t initial) {
  uint16_t crc = initial;
  for (size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(bytes[i]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
  }
  return crc;
}

size_t encodeFrame(uint8_t command, const uint8_t* payload, size_t length,
                   uint8_t* output, size_t capacity) {
  if (length > kMaxPayload || capacity < length + 5 || !output || (length && !payload))
    return 0;
  output[0] = 0;
  output[1] = static_cast<uint8_t>(length + 4);
  output[2] = command;
  if (length) memcpy(output + 3, payload, length);
  const uint16_t crc = crc16(output + 2, length + 1);
  output[length + 3] = static_cast<uint8_t>(crc);
  output[length + 4] = static_cast<uint8_t>(crc >> 8);
  return length + 5;
}

void Parser::discard(size_t count) {
  if (count >= used_) { used_ = 0; return; }
  memmove(bytes_, bytes_ + count, used_ - count);
  used_ -= count;
}

void Parser::expire(uint32_t now) {
  if (used_ && static_cast<uint32_t>(now - lastByte_) > kFrameGapMs) {
    used_ = 0;
    ++gapResets;
  }
}

bool Parser::push(uint8_t byte, uint32_t now, Frame& output) {
  expire(now);
  lastByte_ = now;
  if (used_ == sizeof(bytes_)) discard(1);
  bytes_[used_++] = byte;
  while (used_) {
    if (bytes_[0] != 0) { discard(1); continue; }
    if (used_ < 2) return false;
    const size_t length = bytes_[1];
    if (length < 4 || length > kMaxPayload + 4) {
      ++lengthErrors;
      discard(1);
      continue;
    }
    if (used_ < length + 1) return false;
    const uint16_t stored = bytes_[length - 1] | (static_cast<uint16_t>(bytes_[length]) << 8);
    if (crc16(bytes_ + 2, length - 3) != stored) {
      ++crcErrors;
      discard(1);
      continue;
    }
    output.command = bytes_[2];
    output.length = static_cast<uint8_t>(length - 4);
    if (output.length) memcpy(output.payload, bytes_ + 3, output.length);
    discard(length + 1);
    return true;
  }
  return false;
}

static bool validDate(uint8_t month, uint8_t day, bool leap = true) {
  static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  return month >= 1 && month <= 12 && day >= 1 &&
      day <= days[month - 1] + (month == 2 && leap ? 1 : 0);
}

bool encodeMode(const Mode& value, uint8_t output[4]) {
  if (value.left > 2 || value.right > 2 || value.timers < 1 || value.timers > 3 || value.period > 2)
    return false;
  output[0] = value.left; output[1] = value.right;
  output[2] = value.timers; output[3] = value.period;
  return true;
}

bool encodeWorking(const Working& v, uint8_t out[9]) {
  if (v.days > 127 || v.fromHours > 23 || v.toHours > 23 || v.fromMinutes > 59 ||
      v.toMinutes > 59 || !validDate(v.fromMonth, v.fromDay) || !validDate(v.toMonth, v.toDay))
    return false;
  const uint8_t data[] = {v.days,v.fromHours,v.fromMinutes,v.toHours,v.toMinutes,
                          v.fromMonth,v.fromDay,v.toMonth,v.toDay};
  memcpy(out, data, sizeof(data));
  return true;
}

bool encodeClock(const Clock& v, uint8_t out[7]) {
  if (v.year > 99 || !validDate(v.month, v.day, v.year % 4 == 0) || v.weekDay < 1 ||
      v.weekDay > 7 || v.hours > 23 || v.minutes > 59 || v.seconds > 59) return false;
  const uint8_t data[] = {v.year,v.month,v.day,v.weekDay,v.hours,v.minutes,v.seconds};
  memcpy(out, data, sizeof(data));
  return true;
}

bool encodeCurve(const Curve& v, bool reduction, uint8_t out[32]) {
  if (v.count < 2 || v.count > 16) return false;
  for (uint8_t i = 0; i < v.count; ++i) {
    if (v.points[i].temperature < -35 || v.points[i].temperature > 15 ||
        v.points[i].minutes > (reduction ? 60 : 210) ||
        (i && v.points[i].temperature <= v.points[i-1].temperature)) return false;
  }
  memset(out, 0xff, 32);
  for (uint8_t i = 0; i < v.count; ++i) {
    out[2*i] = static_cast<uint8_t>(v.points[i].temperature);
    out[2*i+1] = v.points[i].minutes;
  }
  return true;
}

static void decodeCurve(const uint8_t* data, Curve& output) {
  output.count = 0;
  for (uint8_t i = 0; i < 16 && data[2*i+1] != 0xff; ++i) {
    // Explicit sign conversion, independent of the host's char signedness.
    output.points[i].temperature = static_cast<int8_t>(data[2*i] < 128 ? data[2*i] : data[2*i] - 256);
    output.points[i].minutes = data[2*i+1];
    ++output.count;
  }
}

bool decodeState(const uint8_t* p, size_t length, State& out) {
  if (!p || length != 91) return false;
  out.mode.left=p[0]; out.mode.right=p[1]; out.mode.timers=p[2]; out.mode.period=p[3];
  out.working.days=p[4]; out.working.fromHours=p[5]; out.working.fromMinutes=p[6];
  out.working.toHours=p[7]; out.working.toMinutes=p[8];
  out.working.fromMonth=p[9]; out.working.fromDay=p[10];
  out.working.toMonth=p[11]; out.working.toDay=p[12];
  decodeCurve(p+13,out.departure); decodeCurve(p+45,out.reduction);
  out.time.year=p[77]; out.time.month=p[78]; out.time.day=p[79]; out.time.weekDay=p[80];
  out.time.hours=p[81]; out.time.minutes=p[82]; out.time.seconds=p[83];
  out.hardwareVersion=p[84] | (static_cast<uint16_t>(p[85]) << 8);
  out.controllerVersion=p[86] | (static_cast<uint16_t>(p[87]) << 8);
  out.storageExponent=p[88]; out.freeKiB=p[89] | (static_cast<uint16_t>(p[90]) << 8);
  return true;
}

int responseLength(uint8_t command) {
  switch (command) {
    case Confirm: case 0x05: case 0x40: return 0;
    case 0x02: case Activity: case Identify: case SetTime: case SetMode:
    case SetWorking: case SetDeparture: case SetReduction: return 1;
    case 0x06: return 8;  // ESP receive table; differs from MCU's ordinary request handler.
    case ReadAll: return 91;
    default: return -1;
  }
}

bool allowedRequest(uint8_t command, size_t length) {
  switch (command) {
    case Confirm: case Activity: case Identify: case ReadAll: return length == 0;
    case SetTime: return length == 7;
    case SetMode: return length == 4;
    case SetWorking: return length == 9;
    case SetDeparture: case SetReduction: return length == 32;
    default: return false;  // Includes all firmware transfer/bootloader operations.
  }
}

const char* jobStatusName(JobStatus status) {
  switch (status) {
    case JobStatus::Queued: return "queued";
    case JobStatus::Sent: return "sent";
    case JobStatus::Applied: return "applied";
    case JobStatus::Rejected: return "rejected";
    case JobStatus::Timeout: return "timeout";
    case JobStatus::TransportError: return "transport_error";
    default: return "none";
  }
}

void Controller::begin(uint32_t now) {
  pending_ = pendingUser_ = confirmed_ = hasState_ = quiet_ = false;
  parser_ = Parser(); job_ = Job();
  heartbeatAt_ = now - 10000U;
  pollAt_ = now - 15000U;
}

bool Controller::online(uint32_t now) const {
  return hasState_ && static_cast<uint32_t>(now - stateAt_) < 45000U;
}

bool Controller::queue(uint8_t command, const uint8_t* payload, size_t length, uint32_t now) {
  if (!online(now) || !confirmed_ || job_.status == JobStatus::Queued || job_.status == JobStatus::Sent ||
      !allowedRequest(command, length) || command == ReadAll || command == Confirm ||
      command == Activity || length > sizeof(queuedPayload_) || (length && !payload)) return false;
  if (length) memcpy(queuedPayload_, payload, length);
  queuedLength_ = static_cast<uint8_t>(length);
  if (++job_.id == 0) ++job_.id;
  job_.command=command; job_.controllerError=0; job_.status=JobStatus::Queued;
  return true;
}

bool Controller::send(uint8_t command, const uint8_t* payload, size_t length,
                      uint32_t now, bool user) {
  uint8_t bytes[kMaxFrame];
  const size_t count=encodeFrame(command,payload,length,bytes,sizeof(bytes));
  if (!count || !sender_(bytes,count,context_)) {
    if (user) job_.status=JobStatus::TransportError;
    return false;
  }
  pending_=true; pendingUser_=user; pendingCommand_=command; sentAt_=now;
  if (user) job_.status=JobStatus::Sent;
  return true;
}

void Controller::tick(uint32_t now) {
  parser_.expire(now);
  if (pending_ && static_cast<uint32_t>(now-sentAt_) >= 700U) {
    ++timeouts;
    if (pendingUser_) job_.status=JobStatus::Timeout;
    pending_=false; pendingUser_=false; quiet_=true; quietAt_=now;
  }
  if (quiet_) {
    if (static_cast<uint32_t>(now-quietAt_) < 1000U) return;
    quiet_=false;
  }
  if (pending_) return;
  if (static_cast<uint32_t>(now-heartbeatAt_) >= 10000U) {
    heartbeatAt_=now;
    send(Confirm,nullptr,0,now,false);
  } else if (static_cast<uint32_t>(now-pollAt_) >= 15000U) {
    // Polling cannot be starved by a stream of configuration writes.
    pollAt_=now;
    send(ReadAll,nullptr,0,now,false);
  } else if (job_.status == JobStatus::Queued) {
    // A queued job must not outlive the evidence that the controller is reachable.
    if (!online(now) || !confirmed_) job_.status=JobStatus::TransportError;
    else send(job_.command,queuedPayload_,queuedLength_,now,true);
  }
}

void Controller::receive(uint8_t byte, uint32_t now) {
  Frame frame;
  if (parser_.push(byte,now,frame)) accept(frame,now);
}

void Controller::accept(const Frame& frame, uint32_t now) {
  ++receivedFrames;
  if (responseLength(frame.command) != frame.length) { ++ignoredFrames; return; }
  if (frame.command == 0x05) {
    ++resetEvents;
    confirmed_=false; hasState_=false;
    heartbeatAt_=now-10000U;
    if (job_.status==JobStatus::Queued || job_.status==JobStatus::Sent)
      job_.status=JobStatus::TransportError;
    pending_=false;
    pendingUser_=false;
    return;
  }
  if (!pending_ || frame.command != pendingCommand_) { ++ignoredFrames; return; }
  pending_=false;
  if (frame.command == Confirm) confirmed_=true;
  if (frame.command == ReadAll && decodeState(frame.payload,frame.length,state_)) {
    hasState_=true; stateAt_=now;
  }
  if (pendingUser_) {
    job_.controllerError=frame.payload[0];
    job_.status=job_.controllerError == 0 ? JobStatus::Applied : JobStatus::Rejected;
    if (job_.status == JobStatus::Applied) pollAt_=now-15000U;
  }
  pendingUser_=false;
}
}  // namespace garo
