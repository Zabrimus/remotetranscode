#pragma once

#include <string>
#include <thread>
#include "process.hpp"
#include "browserclient.h"
#include "vdrclient.h"
#include "transcodeconfig.h"

typedef struct stream_info {
    std::string type;
    std::string codec;
    std::string codec_tag;
    std::string sample_rate;
    std::string bit_rate;
} stream_info;

class FFmpegHandler {
private:
    std::string ffmpeg;

    TinyProcessLib::Process *streamHandler;
    std::thread *readerThread;

public:
    FFmpegHandler(std::string browserIp, int browserPort, std::string vdrIp, int vdrPort, TranscodeConfig& tc, BrowserClient* client, std::string movie_path);
    ~FFmpegHandler();

    std::shared_ptr<std::string>
    probeVideo(std::string url, std::string position, std::string cookies, std::string referer, std::string userAgent, std::string postfix);
    bool streamVideo(std::string url, std::string position, std::string cookies, std::string referer, std::string userAgent);
    bool pauseVideo();
    bool resumeVideo(std::string position);
    void stopVideo();
    bool seekTo(std::string pos);

    bool isRunning() { return readerRunning; };
    bool hasStreamError() { return streamError; };

private:
    std::string browserIp;
    int browserPort;

    int fifo;
    std::string fifoFilename;
    bool readerRunning;
    bool streamError;

    std::string cookies;
    std::string referer;
    std::string userAgent;
    std::string postfix;
    std::string transparentVideoFile;

    std::vector<stream_info> streams;
    std::string duration;
    std::string currentUrl;

    BrowserClient* browserClient;
    VdrClient* vdrClient;

    TranscodeConfig transcodeConfig;
    std::string movie_path;

private:
    std::shared_ptr<std::string> probe(const std::string& url);
    bool createVideoWithLength(std::string seconds, const std::string& name);
    std::shared_ptr<std::string> createVideo(const std::string& url, const std::string& outname);
};
