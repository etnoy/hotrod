#pragma once
#include <stddef.h>
#include <stdint.h>

namespace tinyhttp {
// One bounded HTTP/1.x request. No chunking, keep-alive, uploads or pipelining.
class Request {
 public:
  enum class Result { More, Ready, BadRequest, HeadersTooLarge, BodyTooLarge, MethodNotAllowed,
                      TransferNotSupported, ExpectationNotSupported };
  Result push(char byte);
  void reset();
  const char* method() const { return method_; }
  const char* path() const { return path_; }
  const char* query() const { return query_; }
  const char* authorization() const { return authorization_; }
  const char* contentType() const { return contentType_; }
  const char* body() const { return body_; }
  size_t bodyLength() const { return bodySize_; }
 private:
  Result parseHeaders();
  char headers_[2049] = {}, body_[1025] = {};
  size_t headerSize_ = 0, bodySize_ = 0, contentLength_ = 0;
  bool parsed_ = false;
  Result result_ = Result::More;
  const char *method_ = "", *path_ = "", *query_ = "", *authorization_ = "", *contentType_ = "";
};
}  // namespace tinyhttp
