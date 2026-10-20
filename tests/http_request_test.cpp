#include "HttpRequest.h"
#include <assert.h>
#include <stdio.h>
#include <string>
#include <string.h>

using tinyhttp::Request;
using Result=Request::Result;
static Result feed(Request& request,const std::string& input) {
  Result result=Result::More;
  for (const auto byte:input) result=request.push(byte);
  return result;
}

int main() {
  Request request;
  assert(feed(request,"POST /v1/mode HTTP/1.1\r\nAuthorization: Bearer secret\r\n"
      "Content-Type: application/json\r\nContent-Length: 2\r\n\r\n{")==Result::More);
  assert(request.push('}')==Result::Ready);
  assert(!strcmp(request.path(),"/v1/mode") && !strcmp(request.method(),"POST"));
  assert(!strcmp(request.authorization(),"Bearer secret") && request.bodyLength()==2);
  assert(!strcmp(request.contentType(),"application/json") && !strcmp(request.body(),"{}"));
  request.reset();
  assert(feed(request,"GET /v1/operation?id=123 HTTP/1.0\r\n\r\n")==Result::Ready);
  assert(!strcmp(request.path(),"/v1/operation") && !strcmp(request.query(),"id=123"));
  request.reset();
  assert(feed(request,"POST /v1/time HTTP/1.1\r\ncontent-length: 2 \t\r\n"
      "authorization:\tBearer secret\t\r\n\r\n{}")==Result::Ready);
  assert(!strcmp(request.authorization(),"Bearer secret"));
  const std::string invalid[]={
    "POST / HTTP/1.1\r\nContent-Length: 2\r\nContent-Length: 2\r\n\r\n{}",
    "POST / HTTP/1.1\r\nContent-Length: -1\r\n\r\n",
    "POST / HTTP/1.1\r\nContent-Length: 1x\r\n\r\n",
    "GET / HTTP/1.1\r\nAuthorization: a\r\nAuthorization: b\r\n\r\n",
    "GET / HTTP/1.1\r\nInvalid Header: value\r\n\r\n",
    "GET / HTTP/1.1\r\nContent-Length: 1\r\n\r\nx",
    "GET / HTTP/1.1\n\n",
    "GET / HTTP/2\r\n\r\n",
    "GET /path#fragment HTTP/1.1\r\n\r\n",
    "GET / HTTP/1.1\rX",
    "GET / HTTP/1.1\r\nContent-Type: a\r\nContent-Type: b\r\n\r\n",
  };
  for (const auto& input:invalid) { request.reset(); assert(feed(request,input)==Result::BadRequest); }
  request.reset();
  assert(feed(request,"POST / HTTP/1.1\r\nContent-Length: 1025\r\n\r\n")==Result::BodyTooLarge);
  request.reset();
  assert(feed(request,"POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n")==Result::TransferNotSupported);
  request.reset();
  assert(feed(request,"POST / HTTP/1.1\r\nExpect: 100-continue\r\n\r\n")==Result::ExpectationNotSupported);
  request.reset(); assert(feed(request,"PUT / HTTP/1.1\r\n\r\n")==Result::MethodNotAllowed);
  request.reset();
  assert(feed(request,"GET / HTTP/1.1\r\nX: "+std::string(2048,'a'))==Result::HeadersTooLarge);
  request.reset();
  assert(feed(request,"POST / HTTP/1.1\r\nContent-Length: 1024\r\n\r\n"+std::string(1024,'x'))==Result::Ready);
  request.reset();
  std::string nul="POST / HTTP/1.1\r\nContent-Length: 1\r\n\r\n"; nul.push_back('\0');
  assert(feed(request,nul)==Result::BadRequest);
  // Deterministic arbitrary byte input stays bounded under sanitizers.
  uint32_t rng=0x1d7;
  for (unsigned run=0;run<1000;++run) {
    request.reset();
    for (unsigned i=0;i<4096;++i) { rng=rng*1664525U+1013904223U; request.push(static_cast<char>(rng>>24)); }
  }
  puts("bounded HTTP parsing, complete bodies, auth headers, malformed requests and 4M fuzz bytes passed");
}
