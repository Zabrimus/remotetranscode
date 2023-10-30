#include <string>
#include <chrono>
#include "ffmpeghandler.h"
#include "logger.h"

bool endsWith(const std::string& str, const std::string& suffix) {
    return str.size() >= suffix.size() && 0 == str.compare(str.size()-suffix.size(), suffix.size(), suffix);
}

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

FFmpegHandler::FFmpegHandler(std::string browserIp, int browserPort, std::string vdrIp, int vdrPort, TranscodeConfig& tc, BrowserClient *client, std::string movie_path, const std::string& tmovie) : browserIp(browserIp), browserPort(browserPort), browserClient(client), transcodeConfig(tc), movie_path(movie_path), transparent_movie(tmovie) {
    streamHandler = nullptr;
    streamError = false;
    stopRequest = false;
    vdrClient = new VdrClient(vdrIp, vdrPort);
}

FFmpegHandler::~FFmpegHandler() {
    streamError = false;
    stopRequest = false;
    stopVideo();
    remove((movie_path + "/" + transparentVideoFile).c_str());
    delete vdrClient;
}

std::shared_ptr<std::string> FFmpegHandler::probeVideo(std::string url, std::string position, std::string cookies, std::string referer, std::string userAgent, std::string postfix) {
    currentUrl = url;
    this->cookies = cookies;
    this->referer = referer;
    this->userAgent = userAgent;
    this->postfix = postfix;

    // create transparent video
    DEBUG("Create empty video");
    transparentVideoFile = "transparent-video-" + browserIp + "_" + std::to_string(browserPort) + "-" + this->postfix + ".webm";

    return createVideo(url, transparentVideoFile);
}

bool FFmpegHandler::streamVideo(std::string url, std::string position, std::string cookies, std::string referer, std::string userAgent) {
    currentUrl = url;
    this->cookies = cookies;
    this->referer = referer;
    this->userAgent = userAgent;

    DEBUG("StreamVideo: {} -> {}", position, url);

    // create parameter list
    std::vector<std::string> callStr {
        "ffmpeg", "-hide_banner", "-re", "-y", "-referer", referer, "-user_agent", userAgent, "-headers", "Cookie: " + cookies
    };

    // in case of mpeg-dash ignore the seek command
    if (!endsWith(url, ".mpd")) {
        callStr.emplace_back("-ss");
        callStr.emplace_back(position);
    }

    callStr.emplace_back("-i");
    callStr.emplace_back(url);

    // iterate overall streams
    int audioStreamId = 0;
    int videoStreamId = 0;

    // find best video stream
    int maxVideoBitrate = 0;
    int maxVideoBitrateIndex = -1;
    for (const auto& s : streams) {
        if (s.type == "video") {
            if (std::atoi(s.bit_rate.c_str()) > maxVideoBitrate) {
                maxVideoBitrate = std::atoi(s.bit_rate.c_str());
                maxVideoBitrateIndex = videoStreamId;
            }
            videoStreamId++;
        }
    }

    videoStreamId = 0;

    DEBUG("Video stream with max bitrate {}, index {}", maxVideoBitrate, maxVideoBitrateIndex);

    // add best video stream
    if (maxVideoBitrateIndex >= 0) {
        auto bestVideoStream = streams.at(maxVideoBitrateIndex);

        callStr.emplace_back("-map");
        callStr.emplace_back("0:v:" + std::to_string(maxVideoBitrateIndex));
        callStr.emplace_back("-c:v:" + std::to_string(maxVideoBitrateIndex));

        if (transcodeConfig.isCopyVideo(bestVideoStream.codec)) {
            callStr.emplace_back("copy");

            if (bestVideoStream.codec == "h264" && bestVideoStream.codec_tag == "avc1") {
                callStr.emplace_back("-bsf:v");
                callStr.emplace_back("h264_mp4toannexb");
            }
        } else {
            auto videoParameter = transcodeConfig.getVideoTranscodeParameter();
            callStr.insert(callStr.end(), videoParameter.begin(), videoParameter.end());
        }
    }

    // add audio streams
    for (const auto& s : streams) {
        if (s.type == "audio") {
            callStr.emplace_back("-map");
            callStr.emplace_back("0:a:" + std::to_string(audioStreamId));
            callStr.emplace_back("-c:a:" + std::to_string(audioStreamId));

            if (transcodeConfig.isCopyAudio(s.codec, s.sample_rate)) {
                callStr.emplace_back("copy");
            } else {
                auto audioParameter = transcodeConfig.getAudioTranscodeParameter();
                callStr.insert(callStr.end(), audioParameter.begin(), audioParameter.end());
            }

            audioStreamId++;
        }
    }

    callStr.emplace_back("-f");
    callStr.emplace_back("mpegts");
    // callStr.emplace_back(fifoFilename);
    callStr.emplace_back("pipe:1");

    std::ostringstream paramOut;
    if (!callStr.empty()) {
        std::copy(callStr.begin(), callStr.end() - 1, std::ostream_iterator<std::string>(paramOut, " "));
        paramOut << callStr.back();
    }

    DEBUG("Call ffmpeg: {}", paramOut.str());

    streamHandler = new TinyProcessLib::Process(callStr, "",
        [this](const char *bytes, size_t n) {
            if (stopRequest) {
                return;
            }

            if (hasStreamError()) {
                browserClient->StreamError("Encrypted Stream");
            } else {
                if (!browserClient->Heartbeat()) {
                    // connection problems? abort transcoding
                    ERROR("Unable to connect to browser. Abort transcoding...");
                    // stopVideo();
                    stopRequest = true;
                }

                if (!vdrClient->ProcessTSPacket(std::string(bytes, n))) {
                    // connection problems? abort transcoding
                    ERROR("Unable to connect to vdr. Abort transcoding...");
                    stopRequest = true;
                    // stopVideo();
                }
            }
        },

        [this](const char *bytes, size_t n) {
            // catch errors
            std::string msg = std::string(bytes, n);

            if (msg.find("Failed to seek for auxiliary info") != std::string::npos) {
                // stop reader thread
                streamError = true;
            }

            TRACE("{}", std::string(bytes, n));
        },
        true
    );

    return true;
}

bool FFmpegHandler::pauseVideo() {
    if (streamHandler != nullptr) {
        stopVideo();
    }

    return true;
}

bool FFmpegHandler::resumeVideo(std::string position) {
    streamVideo(currentUrl, position, cookies, referer, userAgent);

    return true;
}

void FFmpegHandler::stopVideo() {
    if (streamHandler != nullptr) {
        streamHandler->kill();
        streamHandler->get_exit_status();
        delete streamHandler;
        streamHandler = nullptr;
    }
}

bool FFmpegHandler::seekTo(std::string pos) {
    pauseVideo();
    resumeVideo(pos);

    return true;
}

std::shared_ptr<std::string> FFmpegHandler::probe(const std::string& url) {
    auto output = std::make_shared<std::string>();
    auto videoResult = std::make_shared<std::string>();

    streams.clear();

    // get stream infos
    DEBUG("Starte ffprobe");
    std::vector<std::string> callStr {
        "ffprobe", "-hide_banner", "-i", url,
        "-loglevel", "quiet",
        "-referer", referer,
        "-user_agent", userAgent,
        "-headers", "Cookie: " + cookies,
        "-print_format", "csv",
        "-show_entries", "format=duration",
        "-show_entries", "stream=codec_type,codec_name,bit_rate,sample_rate,codec_tag_string,width,height,bit_rate"
    };

    TinyProcessLib::Process process(callStr, "", [output](const char *bytes, size_t n) {
        *output += std::string(bytes, n);
    });

    int exit = process.get_exit_status();

    std::ostringstream paramOut;
    if (!callStr.empty()) {
        std::copy(callStr.begin(), callStr.end() - 1, std::ostream_iterator<std::string>(paramOut, " "));
        paramOut << callStr.back();
    }

    DEBUG("Call ffprobe: {}", paramOut.str());
    DEBUG("OUTPUT( {} ) {}", exit, *output);

    *videoResult = "no video";

    std::istringstream input;
    input.str(*output);
    for (std::string line; std::getline(input, line);) {
        if (line.length() == 0) {
            // reset streams (ffprobe is a bit strange)
            streams.clear();
            continue;
        }

        auto parts = split(line, ",");

        if (!parts.empty()) {
            if (parts[0] == "stream") {
                stream_info info;
                info.codec = parts[1];
                info.type = parts[2];
                info.codec_tag = parts[3];

                if (info.type == "audio") {
                    info.sample_rate = parts[4];
                    info.bit_rate = parts[5];
                } else if (info.type == "video") {
                    info.sample_rate = "0";
                    info.bit_rate = parts[6];

                    *videoResult = info.codec + "/" + info.codec_tag + "/" + parts[4] + "/" + parts[5];
                } else {
                    // ignore this
                }

                DEBUG("Found stream: {}, {}, {}, {}, {}", info.type, info.codec, info.codec_tag, info.sample_rate, info.bit_rate);

                streams.push_back(info);
            } else if (parts[0] == "format") {
                duration = parts[1];

                DEBUG("Found duration: {}", duration);
            }
        }
    }

    return videoResult;
}

bool FFmpegHandler::createVideoWithLength(std::string seconds, const std::string& name) {
    auto output = std::make_shared<std::string>();
    auto video = std::make_shared<std::string>();

    if (seconds == "N/A") {
        seconds = "08:00:00.000000000";
    }

    std::vector<std::string> callStr {
        "ffmpeg", "-hide_banner" , "-y", "-i", "pipe:0", "-t", seconds, "-codec", "copy", movie_path + "/" + name // "-f", "webm", "pipe:1" //movie_path + "/" + name
    };

    TinyProcessLib::Process process(callStr, "",
                                    [output, video](const char *bytes, size_t n) {
                                        *video += std::string(bytes, n);
                                    },
                                    [output](const char *bytes, size_t n) {
                                        *output += std::string(bytes, n);
                                    },
                                    true
    );

    process.write(transparent_movie);

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

std::shared_ptr<std::string> FFmpegHandler::createVideo(const std::string& url, const std::string& outname) {
    struct stat sb{};
    if ((stat(outname.c_str(), &sb) != -1) && (remove(outname.c_str()) != 0)) {
        ERROR("File {} exists and delete failed. Aborting...\n", outname.c_str());
        return nullptr;
    }

    std::shared_ptr<std::string> videoInfo = probe(url);

    if (!duration.empty()) {
        if (createVideoWithLength(duration, outname)) {
            return videoInfo;
        }
    } else {
        ERROR("Duration is empty");
    }

    return nullptr;
}