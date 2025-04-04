/**
 * Autogenerated by Thrift Compiler (0.21.0)
 *
 * DO NOT EDIT UNLESS YOU ARE SURE THAT YOU KNOW WHAT YOU ARE DOING
 *  @generated
 */
#include "cefbrowser_types.h"

#include <algorithm>
#include <ostream>

#include <thrift/TToString.h>

namespace cefbrowser {


LoadUrlType::~LoadUrlType() noexcept {
}

LoadUrlType::LoadUrlType() noexcept
   : url() {
}

void LoadUrlType::__set_url(const std::string& val) {
  this->url = val;
}
std::ostream& operator<<(std::ostream& out, const LoadUrlType& obj)
{
  obj.printTo(out);
  return out;
}


uint32_t LoadUrlType::read(::apache::thrift::protocol::TProtocol* iprot) {

  ::apache::thrift::protocol::TInputRecursionTracker tracker(*iprot);
  uint32_t xfer = 0;
  std::string fname;
  ::apache::thrift::protocol::TType ftype;
  int16_t fid;

  xfer += iprot->readStructBegin(fname);

  using ::apache::thrift::protocol::TProtocolException;


  while (true)
  {
    xfer += iprot->readFieldBegin(fname, ftype, fid);
    if (ftype == ::apache::thrift::protocol::T_STOP) {
      break;
    }
    switch (fid)
    {
      case 1:
        if (ftype == ::apache::thrift::protocol::T_STRING) {
          xfer += iprot->readString(this->url);
          this->__isset.url = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      default:
        xfer += iprot->skip(ftype);
        break;
    }
    xfer += iprot->readFieldEnd();
  }

  xfer += iprot->readStructEnd();

  return xfer;
}

uint32_t LoadUrlType::write(::apache::thrift::protocol::TProtocol* oprot) const {
  uint32_t xfer = 0;
  ::apache::thrift::protocol::TOutputRecursionTracker tracker(*oprot);
  xfer += oprot->writeStructBegin("LoadUrlType");

  xfer += oprot->writeFieldBegin("url", ::apache::thrift::protocol::T_STRING, 1);
  xfer += oprot->writeString(this->url);
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldStop();
  xfer += oprot->writeStructEnd();
  return xfer;
}

void swap(LoadUrlType &a, LoadUrlType &b) {
  using ::std::swap;
  swap(a.url, b.url);
  swap(a.__isset, b.__isset);
}

bool LoadUrlType::operator==(const LoadUrlType & rhs) const
{
  if (!(url == rhs.url))
    return false;
  return true;
}

LoadUrlType::LoadUrlType(const LoadUrlType& other0) {
  url = other0.url;
  __isset = other0.__isset;
}
LoadUrlType& LoadUrlType::operator=(const LoadUrlType& other1) {
  url = other1.url;
  __isset = other1.__isset;
  return *this;
}
void LoadUrlType::printTo(std::ostream& out) const {
  using ::apache::thrift::to_string;
  out << "LoadUrlType(";
  out << "url=" << to_string(url);
  out << ")";
}


RedButtonType::~RedButtonType() noexcept {
}

RedButtonType::RedButtonType() noexcept
   : channelId() {
}

void RedButtonType::__set_channelId(const std::string& val) {
  this->channelId = val;
}
std::ostream& operator<<(std::ostream& out, const RedButtonType& obj)
{
  obj.printTo(out);
  return out;
}


uint32_t RedButtonType::read(::apache::thrift::protocol::TProtocol* iprot) {

  ::apache::thrift::protocol::TInputRecursionTracker tracker(*iprot);
  uint32_t xfer = 0;
  std::string fname;
  ::apache::thrift::protocol::TType ftype;
  int16_t fid;

  xfer += iprot->readStructBegin(fname);

  using ::apache::thrift::protocol::TProtocolException;


  while (true)
  {
    xfer += iprot->readFieldBegin(fname, ftype, fid);
    if (ftype == ::apache::thrift::protocol::T_STOP) {
      break;
    }
    switch (fid)
    {
      case 1:
        if (ftype == ::apache::thrift::protocol::T_STRING) {
          xfer += iprot->readString(this->channelId);
          this->__isset.channelId = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      default:
        xfer += iprot->skip(ftype);
        break;
    }
    xfer += iprot->readFieldEnd();
  }

  xfer += iprot->readStructEnd();

  return xfer;
}

uint32_t RedButtonType::write(::apache::thrift::protocol::TProtocol* oprot) const {
  uint32_t xfer = 0;
  ::apache::thrift::protocol::TOutputRecursionTracker tracker(*oprot);
  xfer += oprot->writeStructBegin("RedButtonType");

  xfer += oprot->writeFieldBegin("channelId", ::apache::thrift::protocol::T_STRING, 1);
  xfer += oprot->writeString(this->channelId);
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldStop();
  xfer += oprot->writeStructEnd();
  return xfer;
}

void swap(RedButtonType &a, RedButtonType &b) {
  using ::std::swap;
  swap(a.channelId, b.channelId);
  swap(a.__isset, b.__isset);
}

bool RedButtonType::operator==(const RedButtonType & rhs) const
{
  if (!(channelId == rhs.channelId))
    return false;
  return true;
}

RedButtonType::RedButtonType(const RedButtonType& other2) {
  channelId = other2.channelId;
  __isset = other2.__isset;
}
RedButtonType& RedButtonType::operator=(const RedButtonType& other3) {
  channelId = other3.channelId;
  __isset = other3.__isset;
  return *this;
}
void RedButtonType::printTo(std::ostream& out) const {
  using ::apache::thrift::to_string;
  out << "RedButtonType(";
  out << "channelId=" << to_string(channelId);
  out << ")";
}


StartApplicationType::~StartApplicationType() noexcept {
}

StartApplicationType::StartApplicationType() noexcept
   : channelId(),
     appId(),
     appCookie(),
     appReferrer(),
     appUserAgent(),
     url() {
}

void StartApplicationType::__set_channelId(const std::string& val) {
  this->channelId = val;
}

void StartApplicationType::__set_appId(const std::string& val) {
  this->appId = val;
}

void StartApplicationType::__set_appCookie(const std::string& val) {
  this->appCookie = val;
}

void StartApplicationType::__set_appReferrer(const std::string& val) {
  this->appReferrer = val;
}

void StartApplicationType::__set_appUserAgent(const std::string& val) {
  this->appUserAgent = val;
}

void StartApplicationType::__set_url(const std::string& val) {
  this->url = val;
__isset.url = true;
}
std::ostream& operator<<(std::ostream& out, const StartApplicationType& obj)
{
  obj.printTo(out);
  return out;
}


uint32_t StartApplicationType::read(::apache::thrift::protocol::TProtocol* iprot) {

  ::apache::thrift::protocol::TInputRecursionTracker tracker(*iprot);
  uint32_t xfer = 0;
  std::string fname;
  ::apache::thrift::protocol::TType ftype;
  int16_t fid;

  xfer += iprot->readStructBegin(fname);

  using ::apache::thrift::protocol::TProtocolException;


  while (true)
  {
    xfer += iprot->readFieldBegin(fname, ftype, fid);
    if (ftype == ::apache::thrift::protocol::T_STOP) {
      break;
    }
    switch (fid)
    {
      case 1:
        if (ftype == ::apache::thrift::protocol::T_STRING) {
          xfer += iprot->readString(this->channelId);
          this->__isset.channelId = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      case 2:
        if (ftype == ::apache::thrift::protocol::T_STRING) {
          xfer += iprot->readString(this->appId);
          this->__isset.appId = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      case 3:
        if (ftype == ::apache::thrift::protocol::T_STRING) {
          xfer += iprot->readString(this->appCookie);
          this->__isset.appCookie = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      case 4:
        if (ftype == ::apache::thrift::protocol::T_STRING) {
          xfer += iprot->readString(this->appReferrer);
          this->__isset.appReferrer = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      case 5:
        if (ftype == ::apache::thrift::protocol::T_STRING) {
          xfer += iprot->readString(this->appUserAgent);
          this->__isset.appUserAgent = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      case 6:
        if (ftype == ::apache::thrift::protocol::T_STRING) {
          xfer += iprot->readString(this->url);
          this->__isset.url = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      default:
        xfer += iprot->skip(ftype);
        break;
    }
    xfer += iprot->readFieldEnd();
  }

  xfer += iprot->readStructEnd();

  return xfer;
}

uint32_t StartApplicationType::write(::apache::thrift::protocol::TProtocol* oprot) const {
  uint32_t xfer = 0;
  ::apache::thrift::protocol::TOutputRecursionTracker tracker(*oprot);
  xfer += oprot->writeStructBegin("StartApplicationType");

  xfer += oprot->writeFieldBegin("channelId", ::apache::thrift::protocol::T_STRING, 1);
  xfer += oprot->writeString(this->channelId);
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldBegin("appId", ::apache::thrift::protocol::T_STRING, 2);
  xfer += oprot->writeString(this->appId);
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldBegin("appCookie", ::apache::thrift::protocol::T_STRING, 3);
  xfer += oprot->writeString(this->appCookie);
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldBegin("appReferrer", ::apache::thrift::protocol::T_STRING, 4);
  xfer += oprot->writeString(this->appReferrer);
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldBegin("appUserAgent", ::apache::thrift::protocol::T_STRING, 5);
  xfer += oprot->writeString(this->appUserAgent);
  xfer += oprot->writeFieldEnd();

  if (this->__isset.url) {
    xfer += oprot->writeFieldBegin("url", ::apache::thrift::protocol::T_STRING, 6);
    xfer += oprot->writeString(this->url);
    xfer += oprot->writeFieldEnd();
  }
  xfer += oprot->writeFieldStop();
  xfer += oprot->writeStructEnd();
  return xfer;
}

void swap(StartApplicationType &a, StartApplicationType &b) {
  using ::std::swap;
  swap(a.channelId, b.channelId);
  swap(a.appId, b.appId);
  swap(a.appCookie, b.appCookie);
  swap(a.appReferrer, b.appReferrer);
  swap(a.appUserAgent, b.appUserAgent);
  swap(a.url, b.url);
  swap(a.__isset, b.__isset);
}

bool StartApplicationType::operator==(const StartApplicationType & rhs) const
{
  if (!(channelId == rhs.channelId))
    return false;
  if (!(appId == rhs.appId))
    return false;
  if (!(appCookie == rhs.appCookie))
    return false;
  if (!(appReferrer == rhs.appReferrer))
    return false;
  if (!(appUserAgent == rhs.appUserAgent))
    return false;
  if (__isset.url != rhs.__isset.url)
    return false;
  else if (__isset.url && !(url == rhs.url))
    return false;
  return true;
}

StartApplicationType::StartApplicationType(const StartApplicationType& other4) {
  channelId = other4.channelId;
  appId = other4.appId;
  appCookie = other4.appCookie;
  appReferrer = other4.appReferrer;
  appUserAgent = other4.appUserAgent;
  url = other4.url;
  __isset = other4.__isset;
}
StartApplicationType& StartApplicationType::operator=(const StartApplicationType& other5) {
  channelId = other5.channelId;
  appId = other5.appId;
  appCookie = other5.appCookie;
  appReferrer = other5.appReferrer;
  appUserAgent = other5.appUserAgent;
  url = other5.url;
  __isset = other5.__isset;
  return *this;
}
void StartApplicationType::printTo(std::ostream& out) const {
  using ::apache::thrift::to_string;
  out << "StartApplicationType(";
  out << "channelId=" << to_string(channelId);
  out << ", " << "appId=" << to_string(appId);
  out << ", " << "appCookie=" << to_string(appCookie);
  out << ", " << "appReferrer=" << to_string(appReferrer);
  out << ", " << "appUserAgent=" << to_string(appUserAgent);
  out << ", " << "url="; (__isset.url ? (out << to_string(url)) : (out << "<null>"));
  out << ")";
}


ProcessKeyType::~ProcessKeyType() noexcept {
}

ProcessKeyType::ProcessKeyType() noexcept
   : key() {
}

void ProcessKeyType::__set_key(const std::string& val) {
  this->key = val;
}
std::ostream& operator<<(std::ostream& out, const ProcessKeyType& obj)
{
  obj.printTo(out);
  return out;
}


uint32_t ProcessKeyType::read(::apache::thrift::protocol::TProtocol* iprot) {

  ::apache::thrift::protocol::TInputRecursionTracker tracker(*iprot);
  uint32_t xfer = 0;
  std::string fname;
  ::apache::thrift::protocol::TType ftype;
  int16_t fid;

  xfer += iprot->readStructBegin(fname);

  using ::apache::thrift::protocol::TProtocolException;


  while (true)
  {
    xfer += iprot->readFieldBegin(fname, ftype, fid);
    if (ftype == ::apache::thrift::protocol::T_STOP) {
      break;
    }
    switch (fid)
    {
      case 1:
        if (ftype == ::apache::thrift::protocol::T_STRING) {
          xfer += iprot->readString(this->key);
          this->__isset.key = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      default:
        xfer += iprot->skip(ftype);
        break;
    }
    xfer += iprot->readFieldEnd();
  }

  xfer += iprot->readStructEnd();

  return xfer;
}

uint32_t ProcessKeyType::write(::apache::thrift::protocol::TProtocol* oprot) const {
  uint32_t xfer = 0;
  ::apache::thrift::protocol::TOutputRecursionTracker tracker(*oprot);
  xfer += oprot->writeStructBegin("ProcessKeyType");

  xfer += oprot->writeFieldBegin("key", ::apache::thrift::protocol::T_STRING, 1);
  xfer += oprot->writeString(this->key);
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldStop();
  xfer += oprot->writeStructEnd();
  return xfer;
}

void swap(ProcessKeyType &a, ProcessKeyType &b) {
  using ::std::swap;
  swap(a.key, b.key);
  swap(a.__isset, b.__isset);
}

bool ProcessKeyType::operator==(const ProcessKeyType & rhs) const
{
  if (!(key == rhs.key))
    return false;
  return true;
}

ProcessKeyType::ProcessKeyType(const ProcessKeyType& other6) {
  key = other6.key;
  __isset = other6.__isset;
}
ProcessKeyType& ProcessKeyType::operator=(const ProcessKeyType& other7) {
  key = other7.key;
  __isset = other7.__isset;
  return *this;
}
void ProcessKeyType::printTo(std::ostream& out) const {
  using ::apache::thrift::to_string;
  out << "ProcessKeyType(";
  out << "key=" << to_string(key);
  out << ")";
}


StreamErrorType::~StreamErrorType() noexcept {
}

StreamErrorType::StreamErrorType() noexcept
   : reason() {
}

void StreamErrorType::__set_reason(const std::string& val) {
  this->reason = val;
}
std::ostream& operator<<(std::ostream& out, const StreamErrorType& obj)
{
  obj.printTo(out);
  return out;
}


uint32_t StreamErrorType::read(::apache::thrift::protocol::TProtocol* iprot) {

  ::apache::thrift::protocol::TInputRecursionTracker tracker(*iprot);
  uint32_t xfer = 0;
  std::string fname;
  ::apache::thrift::protocol::TType ftype;
  int16_t fid;

  xfer += iprot->readStructBegin(fname);

  using ::apache::thrift::protocol::TProtocolException;


  while (true)
  {
    xfer += iprot->readFieldBegin(fname, ftype, fid);
    if (ftype == ::apache::thrift::protocol::T_STOP) {
      break;
    }
    switch (fid)
    {
      case 1:
        if (ftype == ::apache::thrift::protocol::T_STRING) {
          xfer += iprot->readString(this->reason);
          this->__isset.reason = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      default:
        xfer += iprot->skip(ftype);
        break;
    }
    xfer += iprot->readFieldEnd();
  }

  xfer += iprot->readStructEnd();

  return xfer;
}

uint32_t StreamErrorType::write(::apache::thrift::protocol::TProtocol* oprot) const {
  uint32_t xfer = 0;
  ::apache::thrift::protocol::TOutputRecursionTracker tracker(*oprot);
  xfer += oprot->writeStructBegin("StreamErrorType");

  xfer += oprot->writeFieldBegin("reason", ::apache::thrift::protocol::T_STRING, 1);
  xfer += oprot->writeString(this->reason);
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldStop();
  xfer += oprot->writeStructEnd();
  return xfer;
}

void swap(StreamErrorType &a, StreamErrorType &b) {
  using ::std::swap;
  swap(a.reason, b.reason);
  swap(a.__isset, b.__isset);
}

bool StreamErrorType::operator==(const StreamErrorType & rhs) const
{
  if (!(reason == rhs.reason))
    return false;
  return true;
}

StreamErrorType::StreamErrorType(const StreamErrorType& other8) {
  reason = other8.reason;
  __isset = other8.__isset;
}
StreamErrorType& StreamErrorType::operator=(const StreamErrorType& other9) {
  reason = other9.reason;
  __isset = other9.__isset;
  return *this;
}
void StreamErrorType::printTo(std::ostream& out) const {
  using ::apache::thrift::to_string;
  out << "StreamErrorType(";
  out << "reason=" << to_string(reason);
  out << ")";
}


InsertHbbtvType::~InsertHbbtvType() noexcept {
}

InsertHbbtvType::InsertHbbtvType() noexcept
   : hbbtv() {
}

void InsertHbbtvType::__set_hbbtv(const std::string& val) {
  this->hbbtv = val;
}
std::ostream& operator<<(std::ostream& out, const InsertHbbtvType& obj)
{
  obj.printTo(out);
  return out;
}


uint32_t InsertHbbtvType::read(::apache::thrift::protocol::TProtocol* iprot) {

  ::apache::thrift::protocol::TInputRecursionTracker tracker(*iprot);
  uint32_t xfer = 0;
  std::string fname;
  ::apache::thrift::protocol::TType ftype;
  int16_t fid;

  xfer += iprot->readStructBegin(fname);

  using ::apache::thrift::protocol::TProtocolException;


  while (true)
  {
    xfer += iprot->readFieldBegin(fname, ftype, fid);
    if (ftype == ::apache::thrift::protocol::T_STOP) {
      break;
    }
    switch (fid)
    {
      case 1:
        if (ftype == ::apache::thrift::protocol::T_STRING) {
          xfer += iprot->readString(this->hbbtv);
          this->__isset.hbbtv = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      default:
        xfer += iprot->skip(ftype);
        break;
    }
    xfer += iprot->readFieldEnd();
  }

  xfer += iprot->readStructEnd();

  return xfer;
}

uint32_t InsertHbbtvType::write(::apache::thrift::protocol::TProtocol* oprot) const {
  uint32_t xfer = 0;
  ::apache::thrift::protocol::TOutputRecursionTracker tracker(*oprot);
  xfer += oprot->writeStructBegin("InsertHbbtvType");

  xfer += oprot->writeFieldBegin("hbbtv", ::apache::thrift::protocol::T_STRING, 1);
  xfer += oprot->writeString(this->hbbtv);
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldStop();
  xfer += oprot->writeStructEnd();
  return xfer;
}

void swap(InsertHbbtvType &a, InsertHbbtvType &b) {
  using ::std::swap;
  swap(a.hbbtv, b.hbbtv);
  swap(a.__isset, b.__isset);
}

bool InsertHbbtvType::operator==(const InsertHbbtvType & rhs) const
{
  if (!(hbbtv == rhs.hbbtv))
    return false;
  return true;
}

InsertHbbtvType::InsertHbbtvType(const InsertHbbtvType& other10) {
  hbbtv = other10.hbbtv;
  __isset = other10.__isset;
}
InsertHbbtvType& InsertHbbtvType::operator=(const InsertHbbtvType& other11) {
  hbbtv = other11.hbbtv;
  __isset = other11.__isset;
  return *this;
}
void InsertHbbtvType::printTo(std::ostream& out) const {
  using ::apache::thrift::to_string;
  out << "InsertHbbtvType(";
  out << "hbbtv=" << to_string(hbbtv);
  out << ")";
}


InsertChannelType::~InsertChannelType() noexcept {
}

InsertChannelType::InsertChannelType() noexcept
   : channel() {
}

void InsertChannelType::__set_channel(const std::string& val) {
  this->channel = val;
}
std::ostream& operator<<(std::ostream& out, const InsertChannelType& obj)
{
  obj.printTo(out);
  return out;
}


uint32_t InsertChannelType::read(::apache::thrift::protocol::TProtocol* iprot) {

  ::apache::thrift::protocol::TInputRecursionTracker tracker(*iprot);
  uint32_t xfer = 0;
  std::string fname;
  ::apache::thrift::protocol::TType ftype;
  int16_t fid;

  xfer += iprot->readStructBegin(fname);

  using ::apache::thrift::protocol::TProtocolException;


  while (true)
  {
    xfer += iprot->readFieldBegin(fname, ftype, fid);
    if (ftype == ::apache::thrift::protocol::T_STOP) {
      break;
    }
    switch (fid)
    {
      case 1:
        if (ftype == ::apache::thrift::protocol::T_STRING) {
          xfer += iprot->readString(this->channel);
          this->__isset.channel = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      default:
        xfer += iprot->skip(ftype);
        break;
    }
    xfer += iprot->readFieldEnd();
  }

  xfer += iprot->readStructEnd();

  return xfer;
}

uint32_t InsertChannelType::write(::apache::thrift::protocol::TProtocol* oprot) const {
  uint32_t xfer = 0;
  ::apache::thrift::protocol::TOutputRecursionTracker tracker(*oprot);
  xfer += oprot->writeStructBegin("InsertChannelType");

  xfer += oprot->writeFieldBegin("channel", ::apache::thrift::protocol::T_STRING, 1);
  xfer += oprot->writeString(this->channel);
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldStop();
  xfer += oprot->writeStructEnd();
  return xfer;
}

void swap(InsertChannelType &a, InsertChannelType &b) {
  using ::std::swap;
  swap(a.channel, b.channel);
  swap(a.__isset, b.__isset);
}

bool InsertChannelType::operator==(const InsertChannelType & rhs) const
{
  if (!(channel == rhs.channel))
    return false;
  return true;
}

InsertChannelType::InsertChannelType(const InsertChannelType& other12) {
  channel = other12.channel;
  __isset = other12.__isset;
}
InsertChannelType& InsertChannelType::operator=(const InsertChannelType& other13) {
  channel = other13.channel;
  __isset = other13.__isset;
  return *this;
}
void InsertChannelType::printTo(std::ostream& out) const {
  using ::apache::thrift::to_string;
  out << "InsertChannelType(";
  out << "channel=" << to_string(channel);
  out << ")";
}


StopVideoType::~StopVideoType() noexcept {
}

StopVideoType::StopVideoType() noexcept
   : reason() {
}

void StopVideoType::__set_reason(const std::string& val) {
  this->reason = val;
}
std::ostream& operator<<(std::ostream& out, const StopVideoType& obj)
{
  obj.printTo(out);
  return out;
}


uint32_t StopVideoType::read(::apache::thrift::protocol::TProtocol* iprot) {

  ::apache::thrift::protocol::TInputRecursionTracker tracker(*iprot);
  uint32_t xfer = 0;
  std::string fname;
  ::apache::thrift::protocol::TType ftype;
  int16_t fid;

  xfer += iprot->readStructBegin(fname);

  using ::apache::thrift::protocol::TProtocolException;


  while (true)
  {
    xfer += iprot->readFieldBegin(fname, ftype, fid);
    if (ftype == ::apache::thrift::protocol::T_STOP) {
      break;
    }
    switch (fid)
    {
      case 1:
        if (ftype == ::apache::thrift::protocol::T_STRING) {
          xfer += iprot->readString(this->reason);
          this->__isset.reason = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      default:
        xfer += iprot->skip(ftype);
        break;
    }
    xfer += iprot->readFieldEnd();
  }

  xfer += iprot->readStructEnd();

  return xfer;
}

uint32_t StopVideoType::write(::apache::thrift::protocol::TProtocol* oprot) const {
  uint32_t xfer = 0;
  ::apache::thrift::protocol::TOutputRecursionTracker tracker(*oprot);
  xfer += oprot->writeStructBegin("StopVideoType");

  xfer += oprot->writeFieldBegin("reason", ::apache::thrift::protocol::T_STRING, 1);
  xfer += oprot->writeString(this->reason);
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldStop();
  xfer += oprot->writeStructEnd();
  return xfer;
}

void swap(StopVideoType &a, StopVideoType &b) {
  using ::std::swap;
  swap(a.reason, b.reason);
  swap(a.__isset, b.__isset);
}

bool StopVideoType::operator==(const StopVideoType & rhs) const
{
  if (!(reason == rhs.reason))
    return false;
  return true;
}

StopVideoType::StopVideoType(const StopVideoType& other14) {
  reason = other14.reason;
  __isset = other14.__isset;
}
StopVideoType& StopVideoType::operator=(const StopVideoType& other15) {
  reason = other15.reason;
  __isset = other15.__isset;
  return *this;
}
void StopVideoType::printTo(std::ostream& out) const {
  using ::apache::thrift::to_string;
  out << "StopVideoType(";
  out << "reason=" << to_string(reason);
  out << ")";
}

} // namespace
