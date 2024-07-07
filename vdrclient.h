#pragma once

#include <string>

#define CPPHTTPLIB_OPENSSL_SUPPORT
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
