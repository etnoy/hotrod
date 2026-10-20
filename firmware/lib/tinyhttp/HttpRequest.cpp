#include "HttpRequest.h"
#include <string.h>

namespace tinyhttp {
static bool equalsIgnoreCase(const char* a,const char* b) {
  while (*a && *b) {
    const char ca=*a>='A' && *a<='Z'?static_cast<char>(*a+32):*a;
    const char cb=*b>='A' && *b<='Z'?static_cast<char>(*b+32):*b;
    if (ca!=cb) return false;
    ++a; ++b;
  }
  return !*a && !*b;
}

void Request::reset() {
  headerSize_=bodySize_=contentLength_=0; parsed_=false; result_=Result::More;
  headers_[0]=body_[0]='\0';
  method_=path_=query_=authorization_=contentType_="";
}

Request::Result Request::push(char byte) {
  if (result_!=Result::More) return result_;
  if (!parsed_) {
    if (headerSize_==sizeof(headers_)-1) return result_=Result::HeadersTooLarge;
    const auto c=static_cast<uint8_t>(byte);
    if (c==0 || c>126 || (c<32 && c!='\r' && c!='\n' && c!='\t'))
      return result_=Result::BadRequest;
    if ((byte=='\n' && (!headerSize_ || headers_[headerSize_-1]!='\r')) ||
        (headerSize_ && headers_[headerSize_-1]=='\r' && byte!='\n'))
      return result_=Result::BadRequest;
    headers_[headerSize_++]=byte; headers_[headerSize_]='\0';
    if (headerSize_>=4 && memcmp(headers_+headerSize_-4,"\r\n\r\n",4)==0)
      result_=parseHeaders();
  } else {
    if (byte==0) return result_=Result::BadRequest;
    if (bodySize_==sizeof(body_)-1) return result_=Result::BodyTooLarge;
    body_[bodySize_++]=byte; body_[bodySize_]='\0';
    if (bodySize_==contentLength_) result_=Result::Ready;
  }
  return result_;
}

Request::Result Request::parseHeaders() {
  char* firstEnd=strstr(headers_,"\r\n");
  if (!firstEnd) return Result::BadRequest;
  *firstEnd='\0';
  char* target=strchr(headers_,' ');
  if (!target) return Result::BadRequest;
  *target++='\0'; method_=headers_;
  char* version=strchr(target,' ');
  if (!version) return Result::BadRequest;
  *version++='\0';
  if (strcmp(version,"HTTP/1.0") && strcmp(version,"HTTP/1.1")) return Result::BadRequest;
  if (target[0]!='/' || strlen(target)>256 || strchr(target,'#')) return Result::BadRequest;
  for (const char* p=target;*p;++p) if (*p<33 || *p>126) return Result::BadRequest;
  path_=target;
  if (char* query=strchr(target,'?')) { *query++='\0'; query_=query; }
  if (strcmp(method_,"GET") && strcmp(method_,"POST")) return Result::MethodNotAllowed;
  bool lengthSeen=false, authorizationSeen=false, typeSeen=false;
  for (char* line=firstEnd+2;*line;) {
    char* end=strstr(line,"\r\n");
    if (!end) return Result::BadRequest;
    *end='\0';
    if (!*line) break;
    char* value=strchr(line,':');
    if (!value || value==line) return Result::BadRequest;
    *value++='\0';
    for (const char* name=line;*name;++name)
      if (!((*name>='a' && *name<='z') || (*name>='A' && *name<='Z') ||
            (*name>='0' && *name<='9') || *name=='-')) return Result::BadRequest;
    while (*value==' ' || *value=='\t') ++value;
    char* valueEnd=value+strlen(value);
    while (valueEnd>value && (valueEnd[-1]==' ' || valueEnd[-1]=='\t')) *--valueEnd='\0';
    if (equalsIgnoreCase(line,"Content-Length")) {
      if (lengthSeen || !*value) return Result::BadRequest;
      lengthSeen=true;
      for (const char* p=value;*p;++p) {
        if (*p<'0' || *p>'9') return Result::BadRequest;
        contentLength_=contentLength_*10+static_cast<size_t>(*p-'0');
        if (contentLength_>sizeof(body_)-1) return Result::BodyTooLarge;
      }
    } else if (equalsIgnoreCase(line,"Transfer-Encoding")) return Result::TransferNotSupported;
    else if (equalsIgnoreCase(line,"Expect")) return Result::ExpectationNotSupported;
    else if (equalsIgnoreCase(line,"Authorization")) {
      if (authorizationSeen) return Result::BadRequest;
      authorizationSeen=true; authorization_=value;
    } else if (equalsIgnoreCase(line,"Content-Type")) {
      if (typeSeen) return Result::BadRequest;
      typeSeen=true; contentType_=value;
    }
    line=end+2;
  }
  if (!strcmp(method_,"GET") && contentLength_) return Result::BadRequest;
  parsed_=true;
  return contentLength_?Result::More:Result::Ready;
}
}  // namespace tinyhttp
