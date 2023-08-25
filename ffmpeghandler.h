#pragma once

#include <string>
#include <thread>
#include "process.hpp"
#include "browserclient.h"
#include "transcodeconfig.h"

typedef struct stream_info {
    std::string type;
    std::string codec;
    std::string codec_tag;
    std::string sample_rate;
} stream_info;

class FFmpegHandler {
private:
    std::string ffmpeg;

    TinyProcessLib::Process *streamHandler;
    std::thread *readerThread;

public:
    FFmpegHandler(std::string browserIp, int browserPort, TranscodeConfig& tc, BrowserClient* client);
    ~FFmpegHandler();

    bool probeVideo(std::string url, std::string position, std::string cookies, std::string referer, std::string userAgent);
    bool streamVideo(std::string url, std::string position, std::string cookies, std::string referer, std::string userAgent);
    bool pauseVideo();
    bool resumeVideo(std::string position);
    void stopVideo();
    bool seekTo(std::string pos);

    bool isRunning() { return readerRunning; };

private:
    std::string browserIp;
    int browserPort;

    int fifo;
    std::string fifoFilename;
    bool readerRunning;

    std::string cookies;
    std::string referer;
    std::string userAgent;

    std::vector<stream_info> streams;
    std::string duration;
    std::string currentUrl;

    BrowserClient* browserClient;

    TranscodeConfig transcodeConfig;

private:
    bool probe(const std::string& url);
    bool createVideoWithLength(std::string seconds, const std::string& name);
    bool createVideo(const std::string& url, const std::string& outname);
};
