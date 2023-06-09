#pragma once

#include <string>
#include <thread>
#include "process.hpp"
#include "browserclient.h"

typedef struct stream_info {
    std::string type;
    std::string codec;
} stream_info;

class FFmpegHandler {
private:
    std::string ffmpeg;

    TinyProcessLib::Process *streamHandler;
    std::thread *readerThread;

public:
    FFmpegHandler(std::string browserIp, int browserPort);
    ~FFmpegHandler();

    bool streamVideo(std::string url);
    bool pauseVideo();
    bool resumeVideo();
    void stopVideo();
    bool seekTo(std::string pos);

    bool isRunning() { return readerRunning; };

private:
    std::string browserIp;
    int browserPort;

    int fifo;
    bool readerRunning;

    std::vector<stream_info> streams;
    std::string duration;

    BrowserClient* browserClient;

private:
    bool probe(const std::string& url);
    bool createVideoWithLength(std::string seconds, const std::string& name);
    bool createVideo(const std::string& url, const std::string& outname);
};
