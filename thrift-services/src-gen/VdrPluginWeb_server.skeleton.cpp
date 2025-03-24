// This autogenerated skeleton file illustrates how to build a server.
// You should copy it to another filename to avoid overwriting it.

#include "VdrPluginWeb.h"
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TSimpleServer.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TBufferTransports.h>

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;

using namespace  ::pluginweb;

class VdrPluginWebHandler : virtual public VdrPluginWebIf {
 public:
  VdrPluginWebHandler() {
    // Your initialization goes here
  }

  bool ProcessOsdUpdate(const ProcessOsdUpdateType& input) {
    // Your implementation goes here
    printf("ProcessOsdUpdate\n");
  }

  bool ProcessOsdUpdateQOI(const ProcessOsdUpdateQOIType& input) {
    // Your implementation goes here
    printf("ProcessOsdUpdateQOI\n");
  }

  bool ProcessTSPacket(const ProcessTSPacketType& input) {
    // Your implementation goes here
    printf("ProcessTSPacket\n");
  }

  bool StartVideo(const StartVideoType& input) {
    // Your implementation goes here
    printf("StartVideo\n");
  }

  bool StopVideo() {
    // Your implementation goes here
    printf("StopVideo\n");
  }

  bool PauseVideo() {
    // Your implementation goes here
    printf("PauseVideo\n");
  }

  bool ResumeVideo() {
    // Your implementation goes here
    printf("ResumeVideo\n");
  }

  bool Seeked() {
    // Your implementation goes here
    printf("Seeked\n");
  }

  bool VideoSize(const VideoSizeType& input) {
    // Your implementation goes here
    printf("VideoSize\n");
  }

  bool VideoFullscreen() {
    // Your implementation goes here
    printf("VideoFullscreen\n");
  }

  bool ResetVideo(const ResetVideoType& input) {
    // Your implementation goes here
    printf("ResetVideo\n");
  }

  bool SelectAudioTrack(const SelectAudioTrackType& input) {
    // Your implementation goes here
    printf("SelectAudioTrack\n");
  }

};

int main(int argc, char **argv) {
  int port = 9090;
  ::std::shared_ptr<VdrPluginWebHandler> handler(new VdrPluginWebHandler());
  ::std::shared_ptr<TProcessor> processor(new VdrPluginWebProcessor(handler));
  ::std::shared_ptr<TServerTransport> serverTransport(new TServerSocket(port));
  ::std::shared_ptr<TTransportFactory> transportFactory(new TBufferedTransportFactory());
  ::std::shared_ptr<TProtocolFactory> protocolFactory(new TBinaryProtocolFactory());

  TSimpleServer server(processor, serverTransport, transportFactory, protocolFactory);
  server.serve();
  return 0;
}

