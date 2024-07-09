#include <string>
#include <chrono>
#include <iostream>
#include <fstream>
#include "ffmpeghandler.h"
#include "logger.h"

std::map<std::string, std::string> transparentVideos;

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

    struct stat sb{};
    if ((stat(transparentVideoFile.c_str(), &sb) != -1) && (remove(transparentVideoFile.c_str()) != 0)) {
        ERROR("File {} exists and delete failed. Aborting...\n", transparentVideoFile.c_str());
        return nullptr;
    }

    std::shared_ptr<std::string> videoInfo;
    if (endsWith(url, ".m3u") || endsWith(url, ".m3u8")) {
        auto m = M3u8Handler(url);
        m3u = m.parseM3u();

        if (m3u.height == 0 && m3u.width == 0) {
            // something went wrong
            return nullptr;
        }

        duration = "N/A";

        videoInfo = std::make_shared<std::string>();
        *videoInfo = std::string("m3u video stream");
    } else {
        videoInfo = probe(url);
    }

    if (videoInfo == nullptr) {
        return nullptr;
    }

    if (createVideoWithLength(duration, transparentVideoFile)) {
        return videoInfo;
    }

    return nullptr;
}

std::vector<std::string> FFmpegHandler::prepareStreamCmd(std::string url, std::string position, std::string cookies, std::string referer, std::string userAgent) {
    // create parameter list
    std::vector<std::string> callStr {
            "ffmpeg", "-hide_banner", "-re", "-y"
    };

    if (!referer.empty()) {
        callStr.push_back("-referer");
        callStr.push_back(referer);
    }

    if (!userAgent.empty()) {
        callStr.push_back("-user_agent");
        callStr.push_back(userAgent);
    }

    if (!cookies.empty()) {
        callStr.push_back("-headers");
        callStr.push_back("Cookie: " + cookies);
    }

    if (transcodeConfig.threads() > 0) {
        callStr.emplace_back("-threads");
        callStr.emplace_back(std::to_string(transcodeConfig.threads()));
    }

    // in case of mpeg-dash ignore the seek command
    if (!endsWith(url, ".mpd")) {
        callStr.emplace_back("-ss");
        callStr.emplace_back(position);
    }

    callStr.emplace_back("-i");
    callStr.emplace_back(url);

    if (!ffmpegCopy) {
        // copy dedicated streams

        callStr.emplace_back("-c:v");
        callStr.emplace_back("copy");
        callStr.emplace_back("-c:a");
        callStr.emplace_back("copy");
        callStr.emplace_back("-ar");
        callStr.emplace_back("48000");

        // add video stream
        for (auto &v: bstreams) {
            if (v["codec_type"] == "video") {
                callStr.emplace_back("-map");
                callStr.emplace_back("0:" + std::to_string((int) v["index"]));
                callStr.emplace_back("-c:" + std::to_string((int) v["index"]));

                if (transcodeConfig.isCopyVideo(v["codec_name"])) {
                    callStr.emplace_back("copy");
                } else {
                    auto videoParameter = transcodeConfig.getVideoTranscodeParameter();
                    callStr.insert(callStr.end(), videoParameter.begin(), videoParameter.end());
                }
            }
        }

        // add audio streams
        for (auto &v: bstreams) {
            if (v["codec_type"] == "audio") {
                callStr.emplace_back("-map");
                callStr.emplace_back("0:" + std::to_string((int) v["index"]));
                callStr.emplace_back("-c:" + std::to_string((int) v["index"]));

                if (transcodeConfig.isCopyAudio(v["codec_name"], v["sample_rate"])) {
                    callStr.emplace_back("copy");
                    callStr.emplace_back("-ar");
                    callStr.emplace_back("48000");
                } else {
                    auto audioParameter = transcodeConfig.getAudioTranscodeParameter();
                    callStr.insert(callStr.end(), audioParameter.begin(), audioParameter.end());
                }
            }
        }
    } else {
        // let ffmpeg decide which streams to copy
        callStr.emplace_back("-codec");
        callStr.emplace_back("copy");
    }

    callStr.emplace_back("-f");
    callStr.emplace_back("mpegts");
    callStr.emplace_back("pipe:1");

    std::ostringstream paramOut;
    if (!callStr.empty()) {
        std::copy(callStr.begin(), callStr.end() - 1, std::ostream_iterator<std::string>(paramOut, " "));
        paramOut << callStr.back();
    }

    DEBUG("Call ffmpeg: {}", paramOut.str());

    return callStr;
}

std::vector<std::string> FFmpegHandler::prepareStreamM3uCmd(std::string url, std::string position, std::string cookies, std::string referer, std::string userAgent) {
    // create parameter list
    std::vector<std::string> callStr {
            "ffmpeg", "-hide_banner", "-re", "-y"
    };

    if (!referer.empty()) {
        callStr.push_back("-referer");
        callStr.push_back(referer);
    }

    if (!userAgent.empty()) {
        callStr.push_back("-user_agent");
        callStr.push_back(userAgent);
    }

    if (!cookies.empty()) {
        callStr.push_back("-headers");
        callStr.push_back("Cookie: " + cookies);
    }

    if (transcodeConfig.threads() > 0) {
        callStr.emplace_back("-threads");
        callStr.emplace_back(std::to_string(transcodeConfig.threads()));
    }

    // in case of mpeg-dash ignore the seek command
    if (!endsWith(url, ".mpd")) {
        callStr.emplace_back("-ss");
        callStr.emplace_back(position);
    }

    // add main input
    callStr.emplace_back("-i");
    callStr.emplace_back(m3u.url);

    // add optional audio input
    std::vector<std::string> audioMetadata;
    for (auto a : m3u.audio) {
        callStr.emplace_back("-i");
        callStr.emplace_back(a.uri);

        // fill audio info in bstreams
        json st = json::parse("{ \"codec_type\": \"audio\", \"tags\": { \"language\": \"" + a.language + "\", \"comment\": \"" + a.name + "\" }}");

        bstreams.push_back(st);
    }

    // transmux
    callStr.emplace_back("-codec");
    callStr.emplace_back("copy");

    // main input
    if (!m3u.audio.empty()) {
        callStr.emplace_back("-map");
        callStr.emplace_back("0:v");

        int idx = 1;
        for (auto a: m3u.audio) {
            callStr.emplace_back("-map");
            callStr.emplace_back(std::to_string(idx) + ":a");
            idx++;
        }

        callStr.insert(std::end(callStr), std::begin(audioMetadata), std::end(audioMetadata));
    }

    callStr.emplace_back("-f");
    callStr.emplace_back("mpegts");
    callStr.emplace_back("pipe:1");

    std::ostringstream paramOut;
    if (!callStr.empty()) {
        std::copy(callStr.begin(), callStr.end() - 1, std::ostream_iterator<std::string>(paramOut, " "));
        paramOut << callStr.back();
    }

    DEBUG("Call ffmpeg: {}", paramOut.str());

    return callStr;
}

bool FFmpegHandler::streamVideo(std::string url, std::string position, std::string cookies, std::string referer, std::string userAgent) {
    currentUrl = url;
    this->cookies = cookies;
    this->referer = referer;
    this->userAgent = userAgent;

    DEBUG("StreamVideo: {} -> {}", position, url);

    // create parameter list
    std::vector<std::string> callStr;

    if (endsWith(url, ".m3u") || endsWith(url, ".m3u8")) {
        DEBUG("Pepare stream video (m3u)");
        callStr = prepareStreamM3uCmd(url, position, cookies, referer, userAgent);
    } else {
        DEBUG("Pepare stream video (non-m3u)");
        callStr = prepareStreamCmd(url, position, cookies, referer, userAgent);
    }

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
                    // This can be happen by intention, e.g. if VDR stops the video player
                    DEBUG("Unable to connect to vdr. Abort transcoding...");
                    stopRequest = true;
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
        streamHandler->kill(true);
        streamHandler->get_exit_status();
        delete streamHandler;
        streamHandler = nullptr;
    }
}

bool FFmpegHandler::seekTo(std::string pos) {
    pauseVideo();
    vdrClient->Seeked();
    resumeVideo(pos);

    return true;
}

std::shared_ptr<std::string> FFmpegHandler::probe(const std::string& url) {
    auto output = std::make_shared<std::string>();
    auto videoResult = std::make_shared<std::string>();

    bstreams.clear();

    // get stream infos
    DEBUG("Starte ffprobe");
    std::vector<std::string> callStr{
            "ffprobe", "-hide_banner", "-i", url,
            "-loglevel", "quiet"
    };

    if (!referer.empty()) {
        callStr.push_back("-referer");
        callStr.push_back(referer);
    }

    if (!userAgent.empty()) {
        callStr.push_back("-user_agent");
        callStr.push_back(userAgent);
    }

    if (!cookies.empty()) {
        callStr.push_back("-headers");
        callStr.push_back("Cookie: " + cookies);
    }

    callStr.push_back("-print_format");
    callStr.push_back("json");

    callStr.push_back("-show_format");

    callStr.push_back("-show_streams");

    callStr.push_back("-show_entries");
    callStr.push_back("format=duration");

    TinyProcessLib::Process process(callStr, "", [output](const char *bytes, size_t n) {
        *output += std::string(bytes, n);
    });

    int exit = process.get_exit_status();

    DEBUG("Probe exit code: {}", exit);
    if (exit == 1) {
        ERROR("Probe of '{}' failed.", url);
        return nullptr;
    }

    std::ostringstream paramOut;
    if (!callStr.empty()) {
        std::copy(callStr.begin(), callStr.end() - 1, std::ostream_iterator<std::string>(paramOut, " "));
        paramOut << callStr.back();
    }

    DEBUG("Call ffprobe: {}", paramOut.str());
    DEBUG("OUTPUT( {} ) {}", exit, *output);

    *videoResult = "no video";

    json st = json::parse(*output);

    ffmpegCopy = false;
    json bestProgram;

    int bestWidth = -1;
    json bestVideo;

    unsigned long bestAudioBitrate = 0;
    json bestAudio;

    auto prgs = st["programs"];

    // search the best video in programs
    for (auto& el1 : prgs.items()) {
        for (auto& el2 : el1.value()) {
            for (auto& el3 : el2) {
                if (el3.contains("width")) {
                    int w = el3["width"];
                    if (w > bestWidth) {
                        bestProgram = el2;
                        bestWidth = w;

                        std::string sample_rate;
                        if (el3.contains("sample_rate")) {
                            sample_rate = el3["sample_rate"];
                        } else {
                            sample_rate = "0";
                        }

                        std::string bit_rate;
                        if (el3.contains("bit_rate")) {
                            sample_rate = el3["bit_rate"];
                        } else {
                            sample_rate = "0";
                        }

                        std::string cn = el3["codec_name"];
                        std::string cnt = el3["codec_tag_string"];
                        *videoResult = cn + "/" + cnt + "/" + sample_rate + "/" + bit_rate;
                    }
                }
            }
        }
    }

    DEBUG("Best Programms, size {}", bestProgram.size());

    // programs not found. search all streams and get best video/audio
    if (bestProgram == nullptr || bestProgram.empty()) {
        for (auto& el : st["streams"]) {
            if (el.contains("width")) {
                int w = el["width"];
                if (w > bestWidth) {
                    bestVideo = el;
                    bestWidth = w;

                    std::string sample_rate;
                    if (el.contains("sample_rate")) {
                        sample_rate = el["sample_rate"];
                    } else {
                        sample_rate = "0";
                    }

                    std::string bit_rate;
                    if (el.contains("bit_rate")) {
                        sample_rate = el["bit_rate"];
                    } else {
                        sample_rate = "0";
                    }

                    std::string cn = el["codec_name"];
                    std::string cnt = el["codec_tag_string"];
                    *videoResult = cn + "/" + cnt + "/" + sample_rate + "/" + bit_rate;
                }
            } else if (el.contains("codec_type") && el["codec_type"] == "audio") {
                // check if the stream has a sample rate
                if (el.contains("sample_rate")) {
                    std::string sr = el["sample_rate"];
                    if (sr == "0") {
                        int idx = el["index"];

                        // it is possible that the valid audio streams are shuffled.
                        // Let ffmpeg decide which streams to copy.
                        ffmpegCopy = true;
                        continue;
                    }

                    if (el.contains("tags")) {
                        if (el["tags"].contains("variant_bitrate")) {
                            std::string br = el["tags"]["variant_bitrate"];
                            unsigned long currentBitrate = std::atol(br.c_str());

                            DEBUG("CurrentBitrate: {}, BestBitrate {}", currentBitrate, bestAudioBitrate);

                            if (currentBitrate > bestAudioBitrate) {
                                bestAudioBitrate = currentBitrate;
                                bestAudio = el;

                                DEBUG("Set Best AUDIO to {}", bestAudio.dump());
                            }
                        } else if (el["tags"].contains("language")) {
                            // always add this audio track
                            bestProgram.push_back(el);
                        } else {
                            bestProgram.push_back(el);
                        }
                    }
                }
            }
        }

        if (bestVideo.size() > 0) {
            bestProgram.push_back(bestVideo);
        }

        if (bestAudio.size() > 0) {
            bestProgram.push_back(bestAudio);
        }
    }

    DEBUG("videoResult: {}", *videoResult);

    auto f = st["format"];
    if (f.contains("duration")) {
        duration = f["duration"];
    } else {
        duration = "N/A";
    }

    bstreams = bestProgram;

    DEBUG("Result of bestStreams: {}", bstreams.dump());

    return videoResult;
}

bool FFmpegHandler::createVideoWithLength(std::string seconds, const std::string& name) {
    auto output = std::make_shared<std::string>();
    std::string video;

    DEBUG("CreateVideoWithLength: seconds:{} seconds, name:{}", seconds, name);

    if (seconds == "N/A") {
        // not necessary to remux, split or anything else
        transparentVideos.erase(name);
        transparentVideos[name] = transparent_movie;
        return true;
    }

    std::vector<std::string> callStr {
        "ffmpeg", "-hide_banner" , "-y", "-i", "pipe:0", "-t", seconds, "-codec", "copy", "-f", "webm", "pipe:1"
    };

    TinyProcessLib::Process process(callStr, "",
                                    [output, &video](const char *bytes, size_t n) {
                                        std::string part = std::string(bytes, n);
                                        video.append(part);
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
        transparentVideos.erase(name);
        transparentVideos[name] = video;
        return true;
    } else {
        ERROR("Call of ffmpeg failed. Tried to create an empty video with length {} seconds and name {}", seconds, name);
        ERROR("Output:\n{}", *output);
        return false;
    }
}

std::string FFmpegHandler::getAudioInfo() {
    json result;

    // add audio streams
    int audioIndex = 0;
    for (auto& v : bstreams) {
        if (v["codec_type"] == "audio") {
            std::string language;
            std::string comment;

            if (v.contains("tags")) {
                language = v["tags"]["language"];
                comment = v["tags"]["comment"];

                json lang;
                lang["index"] = audioIndex;
                lang["language"] = language;
                lang["comment"] = comment;

                result["stream"].push_back(lang);
            }
        }

        audioIndex++;
    }

    return result.dump();
}
