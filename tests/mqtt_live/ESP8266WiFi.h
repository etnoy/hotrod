#pragma once
// Real non-blocking POSIX TCP behind the ESP8266 WiFiClient subset used by MqttBridge.
#include "Arduino.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
class IPAddress {
 public:
  bool fromString(const char* value) { return inet_pton(AF_INET,value,&address)==1; }
  in_addr address = {};
};
constexpr int WL_CONNECTED=3;
struct FakeWifi { int status() const { return WL_CONNECTED; } };
extern FakeWifi WiFi;
class WiFiClient {
 public:
  void setTimeout(uint32_t value) { timeout_=value; }
  void setNoDelay(bool on) { int v=on; if (fd_>=0) setsockopt(fd_,IPPROTO_TCP,TCP_NODELAY,&v,sizeof(v)); }
  bool connect(IPAddress ip,uint16_t port) {
    abort(); fd_=socket(AF_INET,SOCK_STREAM,0); if (fd_<0) return false;
    fcntl(fd_,F_SETFL,fcntl(fd_,F_GETFL)|O_NONBLOCK);
    sockaddr_in a={}; a.sin_family=AF_INET; a.sin_port=htons(port); a.sin_addr=ip.address;
    if (::connect(fd_,reinterpret_cast<sockaddr*>(&a),sizeof(a))<0 && errno!=EINPROGRESS) { abort(); return false; }
    pollfd p={fd_,POLLOUT,0};
    int error=0; socklen_t size=sizeof(error);
    if (poll(&p,1,timeout_)!=1 || getsockopt(fd_,SOL_SOCKET,SO_ERROR,&error,&size) || error) { abort(); return false; }
    return true;
  }
  void abort() { if (fd_>=0) close(fd_); fd_=-1; }
  bool connected() { fill(); return fd_>=0; }
  size_t write(const uint8_t* bytes,size_t length) {
    size_t done=0;
    while (fd_>=0 && done<length) {
      const ssize_t n=send(fd_,bytes+done,length-done,0);
      if (n>0) done+=n; else if (n<0 && errno==EAGAIN) { pollfd p={fd_,POLLOUT,0}; if (poll(&p,1,timeout_)!=1) break; } else break;
    }
    return done;
  }
  int available() { fill(); return buffer_.size(); }
  int read() { fill(); if (buffer_.empty()) return -1; const int v=static_cast<uint8_t>(buffer_.front()); buffer_.erase(0,1); return v; }
 private:
  void fill() {
    char data[512];
    for (;;) {
      if (fd_<0) return;
      const ssize_t n=recv(fd_,data,sizeof(data),0);
      if (n>0) buffer_.append(data,n);
      else if (n==0) { close(fd_); fd_=-1; return; }
      else { if (errno!=EAGAIN && errno!=EINTR) { close(fd_); fd_=-1; } return; }
    }
  }
  int fd_=-1; uint32_t timeout_=100; std::string buffer_;
};
