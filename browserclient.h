#pragma once

#include <string>
#include "httplib.h"

class BrowserClient {
public:
    explicit BrowserClient(std::string browserIp, int browserPort);
    ~BrowserClient();

    bool ProcessTSPacket(std::string packet);

private:
    httplib::Client* client;
};
