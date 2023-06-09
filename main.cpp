#include <iostream>
#include <getopt.h>
#include "mini/ini.h"
#include "httplib.h"
#include "logger.h"
#include "browserclient.h"
#include "ffmpeghandler.h"

std::string transcoderIp;
int transcoderPort;

httplib::Server transcodeServer;

std::map<std::string, FFmpegHandler*> handler;
std::mutex handlerMutex;

void startHttpServer(std::string tIp, int tPort) {

    auto ret = transcodeServer.set_mount_point("/movie", "./movie");
    if (!ret) {
        // must not happen
        ERROR("http mount point ./movie does not exists. Application will not work as desired.");
        return;
    }

    transcodeServer.Post("/StreamUrl", [](const httplib::Request &req, httplib::Response &res) {
        auto url = req.get_param_value("url");
        auto responseIp = req.get_param_value("responseIp");
        auto responsePort = req.get_param_value("responsePort");

        if (url.empty() || responseIp.empty() || responsePort.empty()) {
            res.status = 404;
        } else {
            INFO("StreamUrl {}, stream will be sent to {}:{}", url, responseIp, responsePort);

            auto ffmpeg = new FFmpegHandler(responseIp, std::stoi(responsePort));
            {
                std::lock_guard<std::mutex> guard(handlerMutex);
                handler[responseIp + ":" + responsePort] = ffmpeg;
            }

            DEBUG("Start video streaming...");
            ffmpeg->streamVideo(url);

            res.status = 200;
            res.set_content("ok", "text/plain");
        }
    });

    transcodeServer.Post("/Pause", [](const httplib::Request &req, httplib::Response &res) {
        auto streamId = req.get_param_value("streamId");

        if (streamId.empty()) {
            res.status = 404;
        } else {
            INFO("Pause streamId {}", streamId);

            handler[streamId]->pauseVideo();

            res.status = 200;
            res.set_content("ok", "text/plain");
        }
    });

    transcodeServer.Post("/SeekTo", [](const httplib::Request &req, httplib::Response &res) {
        auto streamId = req.get_param_value("streamId");
        auto seekTo = req.get_param_value("seekTo");

        if (streamId.empty() || seekTo.empty()) {
            res.status = 404;
        } else {
            INFO("SeekTo streamId {}, to {}", streamId, seekTo);

            handler[streamId]->seekTo(seekTo);

            res.status = 200;
            res.set_content("ok", "text/plain");
        }
    });

    transcodeServer.Post("/Resume", [](const httplib::Request &req, httplib::Response &res) {
        auto streamId = req.get_param_value("streamId");

        if (streamId.empty()) {
            res.status = 404;
        } else {
            INFO("Resume streamId {}", streamId);

            handler[streamId]->resumeVideo();

            res.status = 200;
            res.set_content("ok", "text/plain");
        }
    });

    transcodeServer.Post("/Stop", [](const httplib::Request &req, httplib::Response &res) {
        auto streamId = req.get_param_value("streamId");

        if (streamId.empty()) {
            res.status = 404;
        } else {
            INFO("Stop streamId {}", streamId);

            if (handler[streamId] != nullptr) {
                handler[streamId]->stopVideo();
            }

            {
                std::lock_guard<std::mutex> guard(handlerMutex);
                delete handler[streamId];
                handler.erase(streamId);
            }

            res.status = 200;
            res.set_content("ok", "text/plain");
        }
    });

    transcodeServer.listen(tIp, tPort);
}

bool readConfiguration(const char* configFile) {
    mINI::INIFile file(configFile);
    mINI::INIStructure ini;
    auto result = file.read(ini);

    if (!result) {
        ERROR("Unable to read config file: %s", configFile);
        return false;
    }

    try {
        transcoderIp = ini["transcoder"]["http_ip"];
        if (transcoderIp.empty()) {
            ERROR("http ip (transcoder) not found");
            return false;
        }

        std::string tmpTranscoderPort = ini["transcoder"]["http_port"];
        if (tmpTranscoderPort.empty()) {
            ERROR("http port (transcoder) not found");
            return false;
        }

        transcoderPort = std::stoi(tmpTranscoderPort);
    } catch (...) {
        ERROR("configuration error. aborting...");
        return false;
    }

    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " -c <config_file>" << std::endl;
        return -1;
    }

    static struct option long_options[] = {
            { "config",      required_argument, nullptr, 'c' },
            {nullptr }
    };

    int c, option_index = 0;
    while ((c = getopt_long(argc, argv, "c:", long_options, &option_index)) != -1) {
        switch (c) {
            case 'c':
                if (!readConfiguration(optarg)) {
                    exit(-1);
                }
                break;
        }
    }

    // start server
    std::thread t1(startHttpServer, transcoderIp, transcoderPort);
    t1.join();

    return 0;
}
