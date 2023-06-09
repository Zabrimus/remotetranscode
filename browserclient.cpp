#include "browserclient.h"
#include "logger.h"

BrowserClient::BrowserClient(std::string browserIp, int browserPort) {
    client = new httplib::Client(browserIp, browserPort);

    DEBUG("==> 2: BrowserPort = {}", browserPort);
}

BrowserClient::~BrowserClient() {
    delete client;
}

bool BrowserClient::ProcessTSPacket(std::string packet) {
    if (auto res = client->Post("/ProcessTSPacket", packet, "text/plain")) {
        if (res->status != 200) {
            std::cout << "Http result: " << res->status << std::endl;
            return false;
        }
    } else {
        auto err = res.error();
        std::cout << "HTTP error: " << httplib::to_string(err) << std::endl;
        return false;
    }

    return true;
}

