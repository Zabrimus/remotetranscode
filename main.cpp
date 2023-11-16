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

std::string readFile(std::string_view path) {
    constexpr auto read_size = std::size_t{4096};
    auto stream = std::ifstream{path.data()};
    stream.exceptions(std::ios_base::badbit);

    auto out = std::string{};
    auto buf = std::string(read_size, '\0');
    while (stream.read(& buf[0], read_size)) {
        out.append(buf, 0, stream.gcount());
    }
    out.append(buf, 0, stream.gcount());
    return out;
}

inline bool startsWith(const std::string& str, const std::string& prefix) {
    return str.size() >= prefix.size() && 0 == str.compare(0, prefix.size(), prefix);
}

void startHttpServer(std::string tIp, int tPort, std::string movie_path, std::string transparentMovie) {

    httplib::Headers headers;
    headers.emplace("Cache-Control", "no-cache");

    transcodeServer.Post("/Probe", [movie_path, transparentMovie](const httplib::Request &req, httplib::Response &res) {
        std::lock_guard<std::mutex> guard(httpMutex);

        auto url = req.get_param_value("url");
        auto cookies = req.get_param_value("cookies");
        auto referer = req.get_param_value("referer");
        auto userAgent = req.get_param_value("userAgent");
        auto responseIp = req.get_param_value("responseIp");
        auto responsePort = req.get_param_value("responsePort");
        auto vdrIp = req.get_param_value("vdrIp");
        auto vdrPort = req.get_param_value("vdrPort");
        auto postfix = req.get_param_value("postfix");

        if (url.empty() || responseIp.empty() || responsePort.empty() || vdrIp.empty() || vdrPort.empty()) {
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

            auto ffmpeg = new FFmpegHandler(responseIp, std::stoi(responsePort), vdrIp, std::stoi(vdrPort), transcodeConfig, clients[streamId], movie_path, transparentMovie);
            handler[streamId] = ffmpeg;

            DEBUG("Probe video...");
            auto videoInfo = ffmpeg->probeVideo(url, "0", cookies, referer, userAgent, postfix);

            if (videoInfo == nullptr) {
                res.status = 500;
                res.set_content("Unable to probe video", "text/plain");
            } else {
                res.status = 200;
                res.set_content(*videoInfo, "text/plain");
            }
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
        auto vdrIp = req.get_param_value("vdrIp");
        auto vdrPort = req.get_param_value("vdrPort");

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
                return;
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
        auto reason = req.get_param_value("reason");

        if (streamId.empty()) {
            res.status = 404;
        } else {
            INFO("Stop streamId {}, reason {}", streamId, reason);

            if (handler[streamId] != nullptr) {
                handler[streamId]->stopVideo();
            }

            delete handler[streamId];
            handler.erase(streamId);

            res.status = 200;
            res.set_content("ok", "text/plain");
        }
    });

    transcodeServer.Get(R"(/movie/(.*))", [](const httplib::Request &req, httplib::Response &res) {
        std::lock_guard<std::mutex> guard(httpMutex);

        auto name = req.matches[1];

        res.status = 206;
        res.set_content(transparentVideos[name.str()], "video/webm");
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
        std::cerr << "Usage: " << argv[0] << " -c <config_file> -t <transcode_file> -m <movie_path>" << std::endl;
        return -1;
    }

    std::string movie_path = "./movie"; // default value

    static struct option long_options[] = {
            { "config",      required_argument, nullptr, 'c' },
            { "transcode",   optional_argument, nullptr, 't' },
            { "movie",       optional_argument, nullptr, 'm' },
            { "loglevel",    optional_argument, nullptr, 'l' },
            {nullptr }
    };

    int c, option_index = 0, loglevel = 1;
    while ((c = getopt_long(argc, argv, "c:t:m:l:", long_options, &option_index)) != -1) {
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

            case 'm':
                movie_path = std::string(optarg);
                break;

            case 'l':
                loglevel = atoi(optarg);
                break;
        }
    }

    switch (loglevel) {
        case 0:  logger.set_level(spdlog::level::critical); break;
        case 1:  logger.set_level(spdlog::level::err); break;
        case 2:  logger.set_level(spdlog::level::info); break;
        case 3:  logger.set_level(spdlog::level::debug); break;
        case 4:  logger.set_level(spdlog::level::trace); break;
        default: logger.set_level(spdlog::level::err); break;
    }

    // remove all existing temp. transparent video files
    for (const auto & entry : std::filesystem::directory_iterator(movie_path)) {
        if (startsWith(entry.path(), movie_path + "/transparent-video-")) {
            remove(entry.path());
        }
    }

    // read whole transparent file into memory
    std::string transparentMovie = readFile(movie_path + "/transparent-full.webm");

    // start server
    std::thread t1(startHttpServer, transcoderIp, transcoderPort, movie_path, transparentMovie);

    // stop handler if requested
    while (true) {
        for (auto it = handler.cbegin(); it != handler.cend(); ) {
            if (it->second) {
                if (it->second->stopHandler()) {
                    FFmpegHandler* h = it->second;
                    handler.erase(it++);
                    h->stopVideo();
                    delete h;
                } else {
                    ++it;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    t1.join();

    return 0;
}
