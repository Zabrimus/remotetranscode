#include "browserclient.h"
#include "logger.h"

BrowserClient::BrowserClient(std::string browserIp, int browserPort) {
    client = new httplib::Client(browserIp, browserPort);
}

BrowserClient::~BrowserClient() {
    delete client;
}

bool BrowserClient::ProcessTSPacket(std::string packet) {
    if (auto res = client->Post("/ProcessTSPacket", packet, "text/plain")) {
        if (res->status != 200) {
            INFO("[remotetranscoder] Http result(ProcessTSPacket): {}", res->status);
            return false;
        }
    } else {
        auto err = res.error();
        ERROR("[remotetranscoder] Http error(ProcessTSPacket): {}", httplib::to_string(err));
        return false;
    }

    return true;
}

