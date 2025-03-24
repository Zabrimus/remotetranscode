#include <iostream>
#include <getopt.h>
#include <string>
#include <filesystem>
#include "mini/ini.h"

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"

#include "main.h"
#include "logger.h"
#include "streamhandler.h"
#include "transcodeconfig.h"

TThreadPoolServer *thriftServer;

std::string transcoderIp;
int transcoderPort;

std::string browserIp;
int browserPort;

int seekPause;

std::map<std::string, StreamHandler*> handler;

std::mutex httpMutex;

TranscodeConfig transcodeConfig;

std::string kodiPath = "/storage/.kodi";
bool enableKodi = false;

std::string movie_path = "./movie"; // default value
std::string transparentMovie;
bool bindAll = false;

struct SeekToRequests {
    std::string streamId;
    std::string seekTo;
    std::chrono::_V2::system_clock::time_point lastSeek;
};

std::vector<SeekToRequests> seekToVec;
bool stopSeekToThread = false;

inline OperationFailed createException(int code, std::string reason) {
    OperationFailed io;
    io.code = code;
    io.reason = reason;

    return io;
}

RemoteTranscoderServer::~RemoteTranscoderServer() {
}

void RemoteTranscoderServer::ping() {
}

void RemoteTranscoderServer::Probe(std::string &_return, const ProbeType &input) {
    INFO("Probe {}", input.url);

    std::string streamId = input.responseIp + "_" + input.responsePort;

    if (handler[streamId] != nullptr) {
        handler[streamId]->stopVideo();
    }

    delete handler[streamId];
    handler.erase(streamId);

    auto stream = new StreamHandler(input.responseIp, std::stoi(input.responsePort), input.vdrIp, std::stoi(input.vdrPort), transcodeConfig,  movie_path, transparentMovie);
    stream->setKodi(enableKodi, kodiPath);
    handler[streamId] = stream;

    DEBUG("Probe video...");
    auto videoInfo = stream->probeVideo(input.url, "0", input.cookies, input.referer, input.userAgent, input.postfix);

    if (videoInfo == nullptr) {
        throw createException(401, "Unable to probeFfmpeg video");
    } else {
        _return = *videoInfo;
    }
}

bool RemoteTranscoderServer::StreamUrl(const StreamUrlType &input) {
    INFO("StreamUrl {}, stream will be sent to {}:{}", input.url, input.responseIp, input.responsePort);

    std::string streamId = input.responseIp + "_" + input.responsePort;

    // call of probe before StreamUrl is a must
    auto stream = handler[streamId];

    if (stream == nullptr) {
        ERROR("Probe has not been called before StreamUrl. Aborting...");
        throw createException(401, "Probe has not been called before StreamUrl. Aborting...");
    }

    DEBUG("Start video streaming...");
    stream->streamVideo(input.url, input.mpdStart, input.cookies, input.referer, input.userAgent);

    return true;
}

bool RemoteTranscoderServer::Pause(const PauseType &input) {
    INFO("Pause streamId {}", input.streamId);

    if (handler[input.streamId] != nullptr) {
        handler[input.streamId]->pauseVideo();
    }

    return true;
}

bool RemoteTranscoderServer::SeekTo(const SeekToType &input) {
    INFO("SeekTo streamId {}, to {}\n", input.streamId, input.seekTo);

    // search existing seekToRequests
    auto it = seekToVec.begin();
    for (; it != seekToVec.end();) {
        if (it->streamId == input.streamId) {
            it->seekTo = input.seekTo;
            it->lastSeek = std::chrono::high_resolution_clock::now();
            break;
        }
        ++it;
    }

    if (it == seekToVec.end()) {
        // create new entry
        SeekToRequests s;
        s.lastSeek = std::chrono::high_resolution_clock::now();
        s.seekTo = input.seekTo;
        s.streamId = input.streamId;

        TRACE("Add: {}", s.lastSeek.time_since_epoch().count());

        seekToVec.emplace_back(s);
    }

    return true;
}

bool RemoteTranscoderServer::Resume(const ResumeType &input) {
    DEBUG("Resume: StreamId: {}, Position: {}", input.streamId, input.position);

    if (handler[input.streamId] != nullptr) {
        handler[input.streamId]->resumeVideo(input.position);
    }

    return true;
}

bool RemoteTranscoderServer::Stop(const StopType &input) {
    INFO("Stop streamId {}, reason {}", input.streamId, input.reason);

    if (handler[input.streamId] != nullptr) {
        handler[input.streamId]->stopVideo();
        delete handler[input.streamId];
        handler.erase(input.streamId);
    }

    return true;
}

void RemoteTranscoderServer::AudioInfo(std::string &_return, const AudioInfoType &input) {
    DEBUG("AudioInfo: StreamId: {}", input.streamId);

    if (handler[input.streamId] != nullptr) {
        _return = handler[input.streamId]->getAudioInfo();
    } else {
        throw createException(401, "Unknown streamId");
    }
}

void RemoteTranscoderServer::GetVideo(std::string &_return, const VideoType &input) {
    DEBUG("GetVideo: filename: {} with size {}", input.filename, transparentVideos[input.filename].length());

    if (!transparentVideos[input.filename].empty()) {
        _return = transparentVideos[input.filename];
    } else {
        throw createException(401, "Unknown video filename");
    }
}

RemoteTranscoderIf *RemoteTranscoderCloneFactory::getHandler(const TConnectionInfo &connInfo) {
    std::shared_ptr<TSocket> sock = std::dynamic_pointer_cast<TSocket>(connInfo.transport);

    /*
    std::cout << "Incoming connection\n";
    std::cout << "\tSocketInfo: "  << sock->getSocketInfo() << "\n";
    std::cout << "\tPeerHost: "    << sock->getPeerHost() << "\n";
    std::cout << "\tPeerAddress: " << sock->getPeerAddress() << "\n";
    std::cout << "\tPeerPort: "    << sock->getPeerPort() << "\n";
    */

    return new RemoteTranscoderServer();
}

void RemoteTranscoderCloneFactory::releaseHandler(CommonServiceIf *handler) {
    delete handler;
}

void performSeekTo() {
    while (!stopSeekToThread) {
        if (seekToVec.empty()) {
            // nothing to do, wait
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            continue;
        }

        // iterate over all
        std::vector<SeekToRequests>::iterator it = seekToVec.begin();
        for (; it != seekToVec.end();) {
            auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::high_resolution_clock::now() - it->lastSeek).count();

            if (diff >= seekPause) {
                // Seek and delete
                handler[it->streamId]->seekTo(it->seekTo);
                it = seekToVec.erase(it);
            } else {
                ++it;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}

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

void startServer() {
    const int workerCount = 4;
    std::shared_ptr<ThreadManager> threadManager = ThreadManager::newSimpleThreadManager(workerCount);
    threadManager->threadFactory(std::make_shared<ThreadFactory>());
    threadManager->start();

    std::string listenIp = bindAll ? "0.0.0.0" : transcoderIp;

    thriftServer = new TThreadPoolServer(
            std::make_shared<RemoteTranscoderProcessorFactory>(std::make_shared<RemoteTranscoderCloneFactory>()),
            std::make_shared<TServerSocket>(listenIp, transcoderPort),
            std::make_shared<TBufferedTransportFactory>(),
            std::make_shared<TBinaryProtocolFactory>(),
            threadManager);

    thriftServer->serve();
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

void signalHandler ( int signum ) {
    stopSeekToThread = true;
    thriftServer->stop();
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " -c <config_file> -t <transcode_file> -m <movie_path>" << std::endl;
        return -1;
    }

    signal(SIGINT, signalHandler) ;
    signal(SIGTERM, signalHandler) ;

    static struct option long_options[] = {
            { "config",      required_argument, nullptr, 'c' },
            { "transcode",   optional_argument, nullptr, 't' },
            { "movie",       optional_argument, nullptr, 'm' },
            { "loglevel",    optional_argument, nullptr, 'l' },
            { "seekPause",   optional_argument, nullptr, 's' },
            { "bindall",     optional_argument, nullptr, 'b' },
            { "kodipath",    optional_argument, nullptr, 'k' },
            { "enablekodi",  optional_argument, nullptr, 'a' },
            {nullptr }
    };

    int c, option_index = 0, loglevel = 1;
    bindAll = false;

    seekPause = 500;
    while ((c = getopt_long(argc, argv, "c:t:m:l:s:bak:", long_options, &option_index)) != -1) {
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

            case 's':
                seekPause = atoi(optarg);
                break;

            case 'b':
                bindAll = true;
                break;

            case 'a':
                enableKodi = true;
                break;

            case 'k':
                kodiPath = std::string(optarg);
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

    if (enableKodi) {
        INFO("Kodi has been enabled (-a). This is an experimental feature!");
    }

    // read whole transparent file into memory
    transparentMovie = readFile(movie_path + "/transparent-full.webm");

    // check if the mandatory transparent video exists
    if (transparentMovie.empty()) {
        ERROR("Video not found: {}/{}", movie_path, "transparent-full.webm");
        ERROR("Please check the configuration/installation. Abort...");
        exit(1);
    }

    // remove all existing temp. transparent video files
    for (const auto & entry : std::filesystem::directory_iterator(movie_path)) {
        if (startsWith(entry.path(), movie_path + "/transparent-video-")) {
            remove(entry.path());
        }
    }

    // start server
    std::thread t1(startServer);

    // start seekTo handler
    std::thread seekToThread = std::thread(performSeekTo);

    t1.join();
    seekToThread.join();

    return 0;
}

