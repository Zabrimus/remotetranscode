#pragma once

#include <string>
#include "httplib.h"

class BrowserClient {
public:
    explicit BrowserClient(std::string browserIp, int browserPort);
    ~BrowserClient();

    bool ProcessTSPacket(std::string packet);
    bool StreamError(std::string reason);

private:
    httplib::Client* client;
};
