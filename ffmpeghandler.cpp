#include <string>
#include <chrono>
#include "ffmpeghandler.h"
#include "logger.h"

std::vector<std::string> split(std::string s, std::string delimiter) {
    size_t pos_start = 0, pos_end, delim_len = delimiter.length();
    std::string token;
    std::vector<std::string> res;

    while ((pos_end = s.find(delimiter, pos_start)) != std::string::npos) {
        token = s.substr (pos_start, pos_end - pos_start);
        pos_start = pos_end + delim_len;
        res.push_back (token);
    }

    res.push_back (s.substr (pos_start));
    return res;
}

void startReaderThread(int fifo, FFmpegHandler *handler, BrowserClient* client) {
    const auto wait_duration = std::chrono::milliseconds(10) ;

    ssize_t bytes;
    char buffer[32712];

    INFO("Start reader thread...");

    while (handler->isRunning()) {
        if ((bytes = read(fifo, buffer, sizeof(buffer))) > 0) {
            if (!client->ProcessTSPacket(std::string(buffer, bytes))) {
                // connection problems? abort transcoding
                ERROR("Unable to connect to browser. Abort transcoding...");
                handler->stopVideo();
            }
        } else {
            std::this_thread::sleep_for(wait_duration);
        }
    }
}

FFmpegHandler::FFmpegHandler(std::string browserIp, int browserPort) : browserIp(browserIp), browserPort(browserPort) {
    streamHandler = nullptr;
    readerThread = nullptr;
    readerRunning = false;
    fifo = -1;

    browserClient = new BrowserClient(browserIp, browserPort);
}

FFmpegHandler::~FFmpegHandler() {
    readerRunning = false;
    stopVideo();
}

bool FFmpegHandler::streamVideo(std::string url) {
    bool createPipe = false;

    // stop existing video streaming, paranoia
    stopVideo();

    // create transparent video
    DEBUG("Create empty video");
    if (!createVideo(url, "transparent-video-" + browserIp + "_" + std::to_string(browserPort) + ".webm")) {
        return false;
    }

    DEBUG("Create FIFO");
    std::string fifoFilename = "/tmp/ffmpegts_" + browserIp + "_" + std::to_string(browserPort);

    struct stat sb{};
    if(stat(fifoFilename.c_str(), &sb) != -1) {
        if (!S_ISFIFO(sb.st_mode) != 0) {
            if(remove(fifoFilename.c_str()) != 0) {
                ERROR("File {} exists and is not a pipe. Delete failed. Aborting...\n", fifoFilename.c_str());
                return false;
            } else {
                createPipe = true;
            }
        }
    } else {
        createPipe = true;
    }

    if (createPipe) {
        if (mkfifo(fifoFilename.c_str(), 0666) < 0) {
            ERROR("Unable to create pipe {}. Aborting...\n", fifoFilename.c_str());
            return false;
        }
    }

    // TODO: Evt. Transcoding durchführen. Die Commandline muss generischer werden.
    DEBUG("Start transcoder");
    std::vector<std::string> callStr {
        "ffmpeg", "-re", "-y", "-i", url, "-c", "copy", "-f",  "mpegts", fifoFilename
    };

    streamHandler = new TinyProcessLib::Process(callStr, "",
        [](const char *bytes, size_t n) {
            DEBUG("{}", std::string(bytes, n));
        },

        [](const char *bytes, size_t n) {
            DEBUG("{}", std::string(bytes, n));
        },
        true
    );

    if ((fifo = open(fifoFilename.c_str(), O_RDONLY)) < 0) {
        ERROR("FFmpegHandler::streamVideo: {}", strerror(errno));
        return false;
    }

    // start reader thread
    DEBUG("Start reader thread");
    readerRunning = true;
    readerThread = new std::thread(startReaderThread, fifo, this, browserClient);
    readerThread->detach();

    return true;
}

bool FFmpegHandler::pauseVideo() {
    return false;
}

bool FFmpegHandler::resumeVideo() {
    return false;
}

void FFmpegHandler::stopVideo() {
    readerRunning = false;

    if (fifo > 0) {
        close(fifo);
        fifo = -1;
    }

    if (readerThread != nullptr) {
        if (readerThread->joinable()) {
            readerThread->join();
        }
        delete readerThread;
        readerThread = nullptr;
    }

    if (streamHandler != nullptr) {
        streamHandler->kill();
        streamHandler->get_exit_status();
        streamHandler = nullptr;
    }
}

bool FFmpegHandler::seekTo(std::string pos) {
    return false;
}

bool FFmpegHandler::probe(const std::string& url) {
    auto output = std::make_shared<std::string>();

    // get stream infos
    DEBUG("Starte ffprobe");
    std::vector<std::string> callStr {
        "ffprobe", "-y", "-i", url,
        "-loglevel", "quiet",
        "-print_format", "csv",
        "-show_entries", "format=duration",
        "-show_entries", "stream=codec_type,codec_name,bit_rate"
    };

    TinyProcessLib::Process process(callStr, "", [output](const char *bytes, size_t n) {
        DEBUG("STR: {}", std::string(bytes, n));

        *output += std::string(bytes, n);
    });

    int exit = process.get_exit_status();
    if (exit != 0) {
        ERROR("Call of ffprobe failed.");
        return false;
    }

    DEBUG("OUTPUT: {}", *output);

    std::istringstream input;
    input.str(*output);
    for (std::string line; std::getline(input, line);) {
        DEBUG("LINE: {}", line);

        auto parts = split(line, ",");

        if (!parts.empty()) {
            if (parts[0] == "stream") {
                stream_info info;
                info.codec = parts[1];
                info.type = parts[2];

                DEBUG("Found stream: {}, {}", info.type, info.codec);

                streams.push_back(info);
            } else if (parts[0] == "format") {
                duration = parts[1];

                DEBUG("Found duration: {}", duration);
            }
        }
    }

    return true;
}

bool FFmpegHandler::createVideoWithLength(std::string seconds, const std::string& name) {
    auto output = std::make_shared<std::string>();

    if (seconds == "N/A") {
        seconds = "08:00:00.000000000";
    }

    std::vector<std::string> callStr {
        "ffmpeg", "-y", "-i", "movie/transparent-full.webm", "-t", seconds, "-codec", "copy", "movie/" + name
    };

    TinyProcessLib::Process process(callStr, "",
                                    [output](const char *bytes, size_t n) {
                                        *output += std::string(bytes, n);
                                    },
                                    [output](const char *bytes, size_t n) {
                                        *output += std::string(bytes, n);
                                    },
                                    true
    );

    int exit = process.get_exit_status();

    if (exit == 0) {
        DEBUG("ffmpeg:\n {}", *output);
        return true;
    } else {
        ERROR("Call of ffmpeg failed. Tried to create an empty video with length {} seconds and name {}", seconds, name);
        ERROR("Output:\n{}", *output);
        return false;
    }
}

bool FFmpegHandler::createVideo(const std::string& url, const std::string& outname) {
    struct stat sb{};
    if ((stat(outname.c_str(), &sb) != -1) && (remove(outname.c_str()) != 0)) {
        ERROR("File {} exists and delete failed. Aborting...\n", outname.c_str());
        return false;
    }

    if (probe(url)) {
        if (!duration.empty()) {
            return createVideoWithLength(duration, outname);
        } else {
            ERROR("Duration is empty");
        }
    } else {
        ERROR("Probe failed");
    }

    return false;
}
