#pragma once

#include <string>
#include "httplib.h"

class VdrClient {
public:
    explicit VdrClient(std::string vdrIp, int vdrPort);
    ~VdrClient();

    bool ProcessTSPacket(std::string packet);
    bool Seeked();

private:
    httplib::Client* client;
};
