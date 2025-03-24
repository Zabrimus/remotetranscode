#pragma once

#include <string>
#include <thread>
#include "process.hpp"
#include "BrowserClient.h"
#include "VdrClient.h"
#include "transcodeconfig.h"
#include "json.hpp"
#include "m3u8handler.h"

using json = nlohmann::json;

extern std::map<std::string, std::string> transparentVideos;

class StreamHandler {
private:
    std::string ffmpeg;

    TinyProcessLib::Process *streamHandler;
    std::thread *readerThread;

    bool enableKodi;
    std::string kodiPath;
    bool isMpdStream;
    int capabilities;

public:
    StreamHandler(std::string browserIp, int browserPort, std::string vdrIp, int vdrPort, TranscodeConfig& tc, std::string movie_path, const std::string& transparent_movie);
    ~StreamHandler();

    void setKodi(bool enable, std::string path);

    std::shared_ptr<std::string> probeVideo(std::string url, std::string position, std::string cookies, std::string referer, std::string userAgent, std::string postfix);
    bool streamVideo(std::string url, std::string position, std::string cookies, std::string referer, std::string userAgent);
    bool pauseVideo();
    bool resumeVideo(std::string position);
    void stopVideo();
    bool seekTo(std::string pos);

    bool hasStreamError() { return streamError; };
    bool stopHandler() { return stopRequest; };

    std::string getAudioInfo();

    std::string getVideoName() { return transparentVideoFile; };

private:
    std::string browserIp;
    int browserPort;

    bool streamError;
    bool stopRequest;

    std::string cookies;
    std::string referer;
    std::string userAgent;
    std::string postfix;
    std::string transparentVideoFile;

    // non-m3u streams
    json bstreams;
    bool ffmpegCopy;
    std::string duration;

    // m3u streams
    m3u_stream m3u;

    std::string currentUrl;

    BrowserClient* browserClient;
    VdrClient* vdrClient;

    TranscodeConfig transcodeConfig;
    std::string movie_path;
    std::string transparent_movie;


private:
    std::shared_ptr<std::string> probeFfmpeg(const std::string& url);
    std::shared_ptr<std::string> probeDash2ts(const std::string& url);
    bool createVideoWithLength(std::string seconds, const std::string& name);

    std::vector<std::string> prepareStreamCmd(std::string url, std::string position, std::string cookies, std::string referer, std::string userAgent);
    std::vector<std::string> prepareStreamM3uCmd(std::string url, std::string position, std::string cookies, std::string referer, std::string userAgent);
    std::vector<std::string> prepareStreamDash2tsCmd(std::string url, std::string position, std::string cookies, std::string referer, std::string userAgent);
};
