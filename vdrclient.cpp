#include "vdrclient.h"
#include "logger.h"

VdrClient::VdrClient(std::string vdrIp, int vdrPort) {
    client = new httplib::Client(vdrIp, vdrPort);
    client->set_read_timeout(15, 0);
}

VdrClient::~VdrClient() {
    client->stop();
    delete client;
}

bool VdrClient::ProcessTSPacket(std::string packet) {
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

bool VdrClient::Seeked() {
    if (auto res = client->Get("/Seeked")) {
        if (res->status != 200) {
            INFO("[remotetranscoder] Http result(Seeked): {}", res->status);
            return false;
        }
    } else {
        auto err = res.error();
        ERROR("[remotetranscoder] Http error(Seeked): {}", httplib::to_string(err));
        return false;
    }

    return true;
}
