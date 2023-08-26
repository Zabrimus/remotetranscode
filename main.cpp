#include <iostream>
#include <getopt.h>
#include <string>
#include <filesystem>
#include "mini/ini.h"
#include "httplib.h"
#include "logger.h"
#include "ffmpeghandler.h"
#include "transcodeconfig.h"

std::string transcoderIp;
int transcoderPort;

std::string browserIp;
int browserPort;

httplib::Server transcodeServer;

std::map<std::string, FFmpegHandler*> handler;
std::map<std::string, BrowserClient*> clients;

std::mutex httpMutex;

BrowserClient* browserClient;

TranscodeConfig transcodeConfig;

inline bool startsWith(const std::string& str, const std::string& prefix) {
    return str.size() >= prefix.size() && 0 == str.compare(0, prefix.size(), prefix);
}

void startHttpServer(std::string tIp, int tPort) {

    httplib::Headers headers;
    headers.emplace("Cache-Control", "no-cache");

    auto ret = transcodeServer.set_mount_point("/movie", "./movie", headers);
    if (!ret) {
        // must not happen
        ERROR("http mount point ./movie does not exists. Application will not work as desired.");
        return;
    }

    transcodeServer.Post("/Probe", [](const httplib::Request &req, httplib::Response &res) {
        std::lock_guard<std::mutex> guard(httpMutex);

        auto url = req.get_param_value("url");
        auto cookies = req.get_param_value("cookies");
        auto referer = req.get_param_value("referer");
        auto userAgent = req.get_param_value("userAgent");
        auto responseIp = req.get_param_value("responseIp");
        auto responsePort = req.get_param_value("responsePort");
        auto postfix = req.get_param_value("postfix");

        if (url.empty() || responseIp.empty() || responsePort.empty()) {
            res.status = 404;
        } else {
            INFO("Probe {}", url);

            std::string streamId = responseIp + "_" + responsePort;

            if (handler[streamId] != nullptr) {
                handler[streamId]->stopVideo();
            }

            if (clients[streamId] == nullptr) {
                clients[streamId] = new BrowserClient(responseIp, std::stoi(responsePort));
            }

            delete handler[streamId];
            handler.erase(streamId);

            auto ffmpeg = new FFmpegHandler(responseIp, std::stoi(responsePort), transcodeConfig, clients[streamId]);
            handler[streamId] = ffmpeg;

            DEBUG("Probe video...");
            ffmpeg->probeVideo(url, "0", cookies, referer, userAgent, postfix);

            res.status = 200;
            res.set_content("ok", "text/plain");
        }
    });

    transcodeServer.Post("/StreamUrl", [](const httplib::Request &req, httplib::Response &res) {
        std::lock_guard<std::mutex> guard(httpMutex);

        auto url = req.get_param_value("url");
        auto cookies = req.get_param_value("cookies");
        auto referer = req.get_param_value("referer");
        auto userAgent = req.get_param_value("userAgent");
        auto responseIp = req.get_param_value("responseIp");
        auto responsePort = req.get_param_value("responsePort");

        if (url.empty() || responseIp.empty() || responsePort.empty()) {
            res.status = 404;
        } else {
            INFO("StreamUrl {}, stream will be sent to {}:{}", url, responseIp, responsePort);

            std::string streamId = responseIp + "_" + responsePort;

            // call of probe before StreamUrl is a must
            auto ffmpeg = handler[streamId];

            if (ffmpeg == nullptr) {
                ERROR("Probe has not been called before StreamUrl. Aborting...");
                res.status = 500;
                res.set_content("Probe has not been called before StreamUrl. Aborting...", "text/plain");
            }

            DEBUG("Start video streaming...");
            ffmpeg->streamVideo(url, "0", cookies, referer, userAgent);

            res.status = 200;
            res.set_content("ok", "text/plain");
        }
    });

    transcodeServer.Post("/Pause", [](const httplib::Request &req, httplib::Response &res) {
        std::lock_guard<std::mutex> guard(httpMutex);

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
        std::lock_guard<std::mutex> guard(httpMutex);

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
        std::lock_guard<std::mutex> guard(httpMutex);

        auto streamId = req.get_param_value("streamId");
        auto position = req.get_param_value("position");

        DEBUG("Resume: StreamId: {}, Position: {}", streamId, position);

        if (streamId.empty()) {
            res.status = 404;
        } else {
            handler[streamId]->resumeVideo(position);

            res.status = 200;
            res.set_content("ok", "text/plain");
        }
    });

    transcodeServer.Post("/Stop", [](const httplib::Request &req, httplib::Response &res) {
        std::lock_guard<std::mutex> guard(httpMutex);

        auto streamId = req.get_param_value("streamId");

        if (streamId.empty()) {
            res.status = 404;
        } else {
            INFO("Stop streamId {}", streamId);

            if (handler[streamId] != nullptr) {
                handler[streamId]->stopVideo();
            }

            delete handler[streamId];
            handler.erase(streamId);

            res.status = 200;
            res.set_content("ok", "text/plain");
        }
    });

    if (!transcodeServer.listen(tIp, tPort)) {
        CRITICAL("Call of listen failed: ip {}, port {}, Reason: {}", tIp, tPort, strerror(errno));
        exit(1);
    }
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

bool readCodecFile(const char* codecFile) {
    mINI::INIFile file(codecFile);
    mINI::INIStructure ini;
    auto result = file.read(ini);

    if (!result) {
        ERROR("Unable to read codec file: %s", codecFile);
        return false;
    }

    transcodeConfig.createConfiguration(ini);

    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " -c <config_file> -t <transcode_file>" << std::endl;
        return -1;
    }

    static struct option long_options[] = {
            { "config",      required_argument, nullptr, 'c' },
            { "transcode",   optional_argument, nullptr, 't' },
            {nullptr }
    };

    int c, option_index = 0;
    while ((c = getopt_long(argc, argv, "c:t:", long_options, &option_index)) != -1) {
        switch (c) {
            case 'c':
                if (!readConfiguration(optarg)) {
                    exit(-1);
                }
                break;

            case 't':
                if (!readCodecFile(optarg)) {
                    exit(-1);
                }
                break;
        }
    }

    // remove all existing temp. transparent video files
    std::string path = "movie";
    for (const auto & entry : std::filesystem::directory_iterator(path)) {
        if (startsWith(entry.path(), "movie/transparent-video-")) {
            remove(entry.path());
        }
    }

    // start server
    std::thread t1(startHttpServer, transcoderIp, transcoderPort);
    t1.join();

    return 0;
}
