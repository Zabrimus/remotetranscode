#pragma once

#include "RemoteTranscoder.h"

#include <thrift/transport/TSocket.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/server/TThreadPoolServer.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TBufferTransports.h>

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;
using namespace ::apache::thrift::concurrency;
using namespace ::remotetranscoder;
using namespace ::common;

class RemoteTranscoderServer : public RemoteTranscoderIf {

public:
    ~RemoteTranscoderServer() override;

    void ping() override;

    void Probe(std::string& _return, const ProbeType& input) override;
    bool StreamUrl(const StreamUrlType& input) override;
    bool Pause(const PauseType& input) override;
    bool SeekTo(const SeekToType& input) override;
    bool Resume(const ResumeType& input) override;
    bool Stop(const StopType& input) override;
    void AudioInfo(std::string& _return, const AudioInfoType& input) override;
    void GetVideo(std::string& _return, const VideoType& input) override;
};

class RemoteTranscoderCloneFactory : virtual public RemoteTranscoderIfFactory {
public:
    ~RemoteTranscoderCloneFactory() override = default;

    RemoteTranscoderIf* getHandler(const TConnectionInfo& connInfo) override;
    void releaseHandler(CommonServiceIf* handler) override;
};
