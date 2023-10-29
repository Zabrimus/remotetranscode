#include "browserclient.h"
#include "logger.h"

BrowserClient::BrowserClient(std::string browserIp, int browserPort) {
    client = new httplib::Client(browserIp, browserPort);
}

BrowserClient::~BrowserClient() {
    client->stop();
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

bool BrowserClient::StreamError(std::string reason) {
    httplib::Params params;
    params.emplace("reason", reason);

    if (auto res = client->Post("/StreamError", params)) {
        if (res->status != 200) {
            INFO("[remotetranscoder] Http result(StreamError): {}", res->status);
            return false;
        }
    } else {
        auto err = res.error();
        ERROR("[remotetranscoder] Http error(StreamError): {}", httplib::to_string(err));
        return false;
    }

    return true;
}

bool BrowserClient::Heartbeat() {
    if (auto res = client->Get("/Heartbeat")) {
        if (res->status != 200) {
            INFO("[remotetranscoder] Heartbeat, Http result(StreamError): {}", res->status);
            return false;
        }
    } else {
        auto err = res.error();
        ERROR("[remotetranscoder] Heartbeat, Http error(StreamError): {}", httplib::to_string(err));
        return false;
    }

    return true;
}

