#pragma once
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "HttpRequest.h"

// HTTP is serviced incrementally so slow clients cannot block controller keep-alive.
class TinyHttpServer {
 public:
  using Handler=void (*)();
  TinyHttpServer(uint16_t port,Handler handler): listener_(port),handler_(handler) {}
  void begin() { listener_.begin(); listener_.setNoDelay(true); }
  void handleClient() {
    if (!client_ || !client_.connected()) {
      client_.stop(); client_=listener_.accept();
      if (!client_) return;
      client_.setTimeout(100); request_.reset(); extraHeaders_=""; started_=millis();
    }
    if (static_cast<uint32_t>(millis()-started_)>=2000U) {
      send(408,"application/json","{\"error\":\"request_timeout\"}"); return;
    }
    for (size_t n=0;n<256 && client_.available();++n) {
      const auto result=request_.push(static_cast<char>(client_.read()));
      if (result==tinyhttp::Request::Result::More) continue;
      if (result==tinyhttp::Request::Result::Ready) handler_();
      else {
        using Result=tinyhttp::Request::Result;
        int code=400;
        if (result==Result::HeadersTooLarge) code=431;
        else if (result==Result::BodyTooLarge) code=413;
        else if (result==Result::MethodNotAllowed) code=405;
        else if (result==Result::TransferNotSupported) code=501;
        else if (result==Result::ExpectationNotSupported) code=417;
        send(code,"application/json","{\"error\":\"unsupported_or_invalid_request\"}");
      }
      client_.stop(); return;
    }
  }
  String header(const char* key) const {
    if (!strcmp(key,"Authorization")) return request_.authorization();
    if (!strcmp(key,"Content-Type")) return request_.contentType();
    return "";
  }
  String arg(const char* key) const {
    if (!strcmp(key,"plain")) return request_.body();
    if (!strcmp(key,"id") && !strncmp(request_.query(),"id=",3)) return request_.query()+3;
    return "";
  }
  bool hasArg(const char* key) const {
    return !strcmp(key,"id") && !strncmp(request_.query(),"id=",3);
  }
  const char* method() const { return request_.method(); }
  const char* path() const { return request_.path(); }
  void sendHeader(const char* key,const String& value) {
    extraHeaders_+=key; extraHeaders_+=": "; extraHeaders_+=value; extraHeaders_+="\r\n";
  }
  void send(int code,const char* type,const String& body) {
    const String head=String("HTTP/1.1 ")+code+" Response\r\nContent-Type: "+type+
        "\r\nContent-Length: "+body.length()+"\r\nConnection: close\r\n"+extraHeaders_+"\r\n";
    client_.write(reinterpret_cast<const uint8_t*>(head.c_str()),head.length());
    client_.write(reinterpret_cast<const uint8_t*>(body.c_str()),body.length());
    client_.stop();
  }
 private:
  WiFiServer listener_;
  WiFiClient client_;
  Handler handler_;
  tinyhttp::Request request_;
  String extraHeaders_;
  uint32_t started_=0;
};
