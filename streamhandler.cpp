#include <string>
#include <csignal>
#include <thread>
#include <chrono>
#include <iostream>
#include <fstream>
#include "streamhandler.h"
#include "logger.h"

std::map<std::string, std::string> transparentVideos;

std::string getexepath() {
    char result[ PATH_MAX ];
    ssize_t count = readlink( "/proc/self/exe", result, PATH_MAX );
    return std::string(result, static_cast<unsigned long>((count > 0) ? count : 0));
}

bool startsWith(const std::string& str, const std::string& prefix) {
    return str.size() >= prefix.size() && 0 == str.compare(0, prefix.size(), prefix);
}

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

// Network options for an input url. Without them ffmpeg/ffprobe wait forever
// for a hanging server and abort a stream on the first connection reset.
// Only for http(s), other protocols don't know the reconnect options.
static void addNetworkOptions(std::vector<std::string>& callStr, const std::string& url) {
    if (!startsWith(url, "http://") && !startsWith(url, "https://")) {
        return;
    }

    callStr.emplace_back("-rw_timeout");
    callStr.emplace_back("15000000"); // microseconds
    callStr.emplace_back("-reconnect");
    callStr.emplace_back("1");
    callStr.emplace_back("-reconnect_streamed");
    callStr.emplace_back("1");
    callStr.emplace_back("-reconnect_delay_max");
    callStr.emplace_back("5");
}

StreamHandler::StreamHandler(std::string browserIp, int browserPort, std::string vdrIp, int vdrPort, TranscodeConfig& tc, std::string movie_path, const std::string& tmovie) : browserIp(browserIp), browserPort(browserPort), transcodeConfig(tc), movie_path(movie_path), transparent_movie(tmovie) {
    streamHandler = nullptr;
    stopping = false;
    processExited = true;
    ffmpegCopy = false;
    streamError = false;
    stopRequest = false;
    isMpdStream = false;
    enableKodi = false;
    kodiPath = std::string();
    vdrClient = new VdrClient(vdrIp, vdrPort);
    browserClient = new BrowserClient(browserIp, browserPort);
}

StreamHandler::~StreamHandler() {
    streamError = false;
    stopRequest = false;
    stopVideo();
    remove((movie_path + "/" + transparentVideoFile).c_str());

    // the transparent video is kept in memory, every stream has its own one
    transparentVideos.erase(transparentVideoFile);
    delete vdrClient;
    delete browserClient;
}

void StreamHandler::setKodi(bool enable, std::string path) {
    this->enableKodi = enable;
    this->kodiPath = std::move(path);
}

std::shared_ptr<std::string> StreamHandler::probeVideo(std::string url, std::string position, std::string cookies, std::string referer, std::string userAgent, std::string postfix) {
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
        auto m = M3u8Handler(url, userAgent, referer, cookies);
        m3u = m.parseM3u();

        if (m3u.height == 0 && m3u.width == 0) {
            // something went wrong
            return nullptr;
        }

        duration = "N/A";

        // the audio infos are filled once per probe, not on every (re)start of the stream
        bstreams = json::array();
        for (const auto& a : m3u.audio) {
            json st;
            st["codec_type"] = "audio";
            st["tags"]["language"] = a.language;
            st["tags"]["comment"] = a.name;
            bstreams.push_back(st);
        }

        videoInfo = std::make_shared<std::string>();
        *videoInfo = std::string("m3u video stream");
    } else if (enableKodi && (endsWith(url, ".mpd") || (url.find("hbbtv.zdf.de/zdfm3/dyn/mpd.php") != std::string::npos))) {
        videoInfo = probeDash2ts(url);
        isMpdStream = true;
    } else {
        videoInfo = probeFfmpeg(url);
    }

    if (videoInfo == nullptr) {
        return nullptr;
    }

    if (createVideoWithLength(duration, transparentVideoFile)) {
        return videoInfo;
    }

    return nullptr;
}

std::vector<std::string> StreamHandler::prepareStreamCmd(std::string url, std::string position, std::string cookies, std::string referer, std::string userAgent) {
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
    if (!endsWith(url, ".mpd") && (url.find("hbbtv.zdf.de/zdfm3/dyn/mpd.php") == std::string::npos)) {
        callStr.emplace_back("-ss");
        callStr.emplace_back(position);
    }

    addNetworkOptions(callStr, url);
    callStr.emplace_back("-i");
    callStr.emplace_back(url);

    if (!ffmpegCopy) {
        // copy dedicated streams

        callStr.emplace_back("-c:v");
        callStr.emplace_back("copy");
        callStr.emplace_back("-c:a");
        callStr.emplace_back("copy");

        // add video stream
        // Note: the stream specifier of -c refers to the output stream index,
        // which is the order of the -map options, not the input stream index.
        int outIndex = 0;
        for (auto &v: bstreams) {
            if (v["codec_type"] == "video") {
                callStr.emplace_back("-map");
                callStr.emplace_back("0:" + std::to_string((int) v["index"]));
                callStr.emplace_back("-c:" + std::to_string(outIndex));

                if (transcodeConfig.isCopyVideo(v["codec_name"])) {
                    callStr.emplace_back("copy");
                } else {
                    auto videoParameter = transcodeConfig.getVideoTranscodeParameter();
                    callStr.insert(callStr.end(), videoParameter.begin(), videoParameter.end());
                }

                outIndex++;
            }
        }

        // add audio streams
        for (auto &v: bstreams) {
            if (v["codec_type"] == "audio") {
                callStr.emplace_back("-map");
                callStr.emplace_back("0:" + std::to_string((int) v["index"]));
                callStr.emplace_back("-c:" + std::to_string(outIndex));

                if (transcodeConfig.isCopyAudio(v["codec_name"], v["sample_rate"])) {
                    callStr.emplace_back("copy");
                } else {
                    auto audioParameter = transcodeConfig.getAudioTranscodeParameter();
                    callStr.insert(callStr.end(), audioParameter.begin(), audioParameter.end());

                    // a sample rate can only be set if the audio is encoded
                    callStr.emplace_back("-ar:" + std::to_string(outIndex));
                    callStr.emplace_back("48000");
                }

                outIndex++;
            }
        }

        // add DVB subtitle streams, other subtitle formats can't be put into the TS
        for (auto &v: bstreams) {
            if (v.value("codec_type", "") == "subtitle" && v.value("codec_name", "") == "dvb_subtitle") {
                callStr.emplace_back("-map");
                callStr.emplace_back("0:" + std::to_string((int) v["index"]));
                callStr.emplace_back("-c:" + std::to_string(outIndex));
                callStr.emplace_back("copy");

                outIndex++;
            }
        }
    } else {
        // let ffmpeg decide which streams to copy. Its automatic selection may choose a
        // text subtitle, which can't be copied into a TS and aborts the whole stream.
        callStr.emplace_back("-codec");
        callStr.emplace_back("copy");
        callStr.emplace_back("-sn");
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

std::vector<std::string> StreamHandler::prepareStreamM3uCmd(std::string url, std::string position, std::string cookies, std::string referer, std::string userAgent) {
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
    if (!endsWith(url, ".mpd") && (url.find("hbbtv.zdf.de/zdfm3/dyn/mpd.php") == std::string::npos)) {
        callStr.emplace_back("-ss");
        callStr.emplace_back(position);
    }

    // add main input
    addNetworkOptions(callStr, m3u.url);
    callStr.emplace_back("-i");
    callStr.emplace_back(m3u.url);

    // add optional audio input. -ss is an input option and must be repeated
    // for every input, otherwise the audio starts at 0 after a seek or resume.
    std::vector<std::string> audioMetadata;
    for (auto a : m3u.audio) {
        if (!endsWith(url, ".mpd") && (url.find("hbbtv.zdf.de/zdfm3/dyn/mpd.php") == std::string::npos)) {
            callStr.emplace_back("-ss");
            callStr.emplace_back(position);
        }

        // the http options are input options, they apply to the next input only
        if (!referer.empty()) {
            callStr.emplace_back("-referer");
            callStr.emplace_back(referer);
        }

        if (!userAgent.empty()) {
            callStr.emplace_back("-user_agent");
            callStr.emplace_back(userAgent);
        }

        if (!cookies.empty()) {
            callStr.emplace_back("-headers");
            callStr.emplace_back("Cookie: " + cookies);
        }

        addNetworkOptions(callStr, a.uri);
        callStr.emplace_back("-i");
        callStr.emplace_back(a.uri);
    }

    // transmux. HLS subtitles (WebVTT) can't be copied into a TS.
    callStr.emplace_back("-codec");
    callStr.emplace_back("copy");
    callStr.emplace_back("-sn");

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

        // audio contained in the variant itself (rendition without URI), if any.
        // Mapped last, so the indexes of the audio infos match the order in the TS.
        callStr.emplace_back("-map");
        callStr.emplace_back("0:a?");

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

std::vector<std::string>
StreamHandler::prepareStreamDash2tsCmd(std::string url, std::string position, std::string cookies, std::string referer,
                                       std::string userAgent) {
    std::string exepath = getexepath();
    std::string dash2ts_bin = exepath.substr(0, exepath.find_last_of('/')) + "/r_dash2ts";

    // get stream infos
    DEBUG("Starte r_dash2ts");
    std::vector<std::string> callStr{
            dash2ts_bin,
            "-u", url,
            "-a", userAgent,
            "-c", cookies,
            "-r", referer,
            "-k", kodiPath,
            "-s", position
    };

    return callStr;
}

bool StreamHandler::streamVideo(std::string url, std::string position, std::string cookies, std::string referer, std::string userAgent) {
    currentUrl = url;
    this->cookies = cookies;
    this->referer = referer;
    this->userAgent = userAgent;

    DEBUG("StreamVideo: {} -> {}", position, url);

    // never run two ffmpeg processes for the same stream
    stopVideo();

    // a new stream starts without the errors of the previous one
    stopRequest = false;
    streamError = false;

    // create parameter list
    std::vector<std::string> callStr;

    if (endsWith(url, ".m3u") || endsWith(url, ".m3u8")) {
        DEBUG("Pepare stream video (m3u)");
        callStr = prepareStreamM3uCmd(url, position, cookies, referer, userAgent);
    } else if (isMpdStream) {
        DEBUG("Pepare stream video (dash2ts)");
        callStr = prepareStreamDash2tsCmd(url, position, cookies, referer, userAgent);
    } else {
        DEBUG("Pepare stream video (non-m3u)");
        callStr = prepareStreamCmd(url, position, cookies, referer, userAgent);
    }

    stopping = false;

    TinyProcessLib::Process *process = new TinyProcessLib::Process(callStr, "",
        [this](const char *bytes, size_t n) {
            if (stopRequest) {
                return;
            }

            if (hasStreamError()) {
                // report the error only once and stop the stream
                browserClient->StreamError("Encrypted Stream");
                stopRequest = true;
                terminateStream();
            } else {
                std::string packets = std::string(bytes, n);
                if (!vdrClient->ProcessTSPacket(packets)) {
                    // connection problems? abort transcoding
                    // This can be happen by intention, e.g. if VDR stops the video player
                    DEBUG("Unable to connect to vdr. Abort transcoding...");
                    stopRequest = true;
                    terminateStream();
                }
            }
        },

        [this](const char *bytes, size_t n) {
            // catch errors
            std::string msg = std::string(bytes, n);

            if (isMpdStream) {
                fprintf(stderr, "%s\n", msg.c_str());
            }

            if (msg.find("Failed to seek for auxiliary info") != std::string::npos) {
                // stop reader thread
                streamError = true;
            }

            TRACE("{}", std::string(bytes, n));
        },
        true
    );

    streamHandler = process;

    // The process ends at the end of the stream (exit code 0). get_exit_status() returns
    // after all output has been delivered, so VDR has got all packets at this time.
    processExited = false;
    exitWatcher = std::thread([this, process]() {
        int exit = process->get_exit_status();
        processExited = true;
        DEBUG("Stream process finished with exit code {}", exit);

        if (exit == 0 && !stopping && !stopRequest) {
            vdrClient->EndOfStream();
        }
    });

    return true;
}

bool StreamHandler::pauseVideo() {
    // the MSE stream continues after the pause
    if (mse) {
        mse->pause();
        return true;
    }

    if (streamHandler != nullptr) {
        stopVideo();
    }

    return true;
}

bool StreamHandler::resumeVideo(std::string position) {
    if (mse) {
        mse->resume();
        return true;
    }

    streamVideo(currentUrl, position, cookies, referer, userAgent);

    return true;
}

void StreamHandler::terminateStream() {
    // called from the reader thread: only signal ffmpeg, stopVideo() cleans up
    TinyProcessLib::Process *p = streamHandler;
    if (p != nullptr) {
        p->kill();
    }
}

void StreamHandler::stopVideo() {
    mse.reset();

    // the reader thread may still access streamHandler until the process has finished
    TinyProcessLib::Process *p = streamHandler.load();
    if (p != nullptr) {
        stopping = true;

        // The child creates its process group after the fork. A signal to the group
        // before is lost and ffmpeg runs until the end of the video: repeat it.
        for (int i = 0; i < 500 && !processExited; ++i) {
            if (i % 10 == 0) {
                p->kill(true);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (!processExited) {
            ERROR("Stream process {} doesn't stop, kill it", (int) p->get_id());
            ::kill(-p->get_id(), SIGKILL);
            ::kill(p->get_id(), SIGKILL);
        }

        // the watcher waits for the process and its reader threads
        if (exitWatcher.joinable()) {
            exitWatcher.join();
        }

        streamHandler = nullptr;
        delete p;
    }
}

bool StreamHandler::seekTo(std::string pos) {
    // the browser resets a MSE stream itself, the next segments follow
    if (mse) {
        return true;
    }

    pauseVideo();
    vdrClient->Seeked();
    resumeVideo(pos);

    return true;
}

std::shared_ptr<std::string> StreamHandler::probeFfmpeg(const std::string& url) {
    auto output = std::make_shared<std::string>();
    auto videoResult = std::make_shared<std::string>();

    bstreams.clear();

    // get stream infos
    DEBUG("Starte ffprobe");
    std::vector<std::string> callStr{
            "ffprobe", "-hide_banner", "-i", url,
            "-loglevel", "quiet"
    };

    addNetworkOptions(callStr, url);

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
    if (exit != 0) {
        ERROR("Probe of '{}' failed with exit code {}.", url, exit);
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

    json st;
    try {
        st = json::parse(*output);
    } catch (json::exception &e) {
        ERROR("Probe of '{}' returned no usable result: {}", url, e.what());
        return nullptr;
    }

    if (!st.is_object() || !st.contains("streams")) {
        ERROR("Probe of '{}' found no streams.", url);
        return nullptr;
    }

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
                        *videoResult = cn + "/" + cnt;
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
                    *videoResult = cn + "/" + cnt;
                }
            } else if (el.value("codec_type", "") == "subtitle" && el.value("codec_name", "") == "dvb_subtitle") {
                // DVB subtitles can be copied into the TS, VDR displays them itself
                bestProgram.push_back(el);
            } else if (el.contains("codec_type") && el["codec_type"] == "audio") {
                // check if the stream has a sample rate
                if (el.contains("sample_rate")) {
                    std::string sr = el["sample_rate"];
                    if (sr == "0") {
                        // int idx = el["index"];

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
                    } else {
                        // audio without any tags
                        bestProgram.push_back(el);
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

std::shared_ptr<std::string> StreamHandler::probeDash2ts(const std::string& url) {
    auto output = std::make_shared<std::string>();
    auto videoResult = std::make_shared<std::string>();

    bstreams.clear();

    std::string exepath = getexepath();
    std::string dash2ts_bin = exepath.substr(0, exepath.find_last_of('/')) + "/r_dash2ts";

    // get stream infos
    std::vector<std::string> callStr {
            dash2ts_bin,
            "-u", url,
            "-a", userAgent,
            "-r", referer,
            "-k", kodiPath,
            "-p"
    };

    if (!cookies.empty()) {
        callStr.emplace_back("-c");
        callStr.emplace_back(cookies);
    }

    DEBUG("Starte r_dash2ts");
    DEBUG("{} -u \"{}\" -a \"{}\" -c \"{}\" -r \"{}\" -k \"{}\" -p", dash2ts_bin, url, userAgent, cookies, referer, kodiPath);

    TinyProcessLib::Process process(callStr, "", [output](const char *bytes, size_t n) {
        *output += std::string(bytes, n);
    });

    int exit = process.get_exit_status();

    DEBUG("Probe exit code: {}", exit);
    if (exit != 0) {
        ERROR("Probe of '{}' failed with exit code {}.", url, exit);
        return nullptr;
    }

    std::ostringstream paramOut;
    if (!callStr.empty()) {
        std::copy(callStr.begin(), callStr.end() - 1, std::ostream_iterator<std::string>(paramOut, " "));
        paramOut << callStr.back();
    }

    DEBUG("Call r_dash2ts: {}", paramOut.str());
    DEBUG("OUTPUT( {} ) {}", exit, *output);

    int tdur = 0;
    int ttime = 0;
    int tcap = 0;
    long pos = 0;
    long length = 0;
    int realTime = 0;

    auto idx = output->find("dash2ts result:");
    if (idx == std::string::npos ||
        sscanf(output->substr(idx).c_str(), "dash2ts result:%d:%d:%d:%ld:%ld:%d", &tdur, &ttime, &tcap, &pos, &length, &realTime) != 6) {
        ERROR("Probe of '{}' returned no usable result.", url);
        return nullptr;
    }

    duration = std::to_string(tdur / 1000 + 1);
    capabilities = tcap;

    DEBUG("Duration: {}", (tdur / 1000));
    DEBUG("Time:     {}", ttime);
    DEBUG("Position: {}", pos);
    DEBUG("Length:   {}", length);
    DEBUG("RealTime: {}", realTime ? "yes" : "no");
    DEBUG("Capabilities");
    DEBUG("   - Demux support:    {}", capabilities & (1 << 0) ? "yes" : "no");
    DEBUG("   - Time support:     {}", capabilities & (1 << 1) ? "yes" : "no");
    DEBUG("   - Times support:    {}", capabilities & (1 << 2) ? "yes" : "no");
    DEBUG("   - Seek support:     {}", capabilities & (1 << 3) ? "yes" : "no");
    DEBUG("   - Pause support:    {}", capabilities & (1 << 4) ? "yes" : "no");
    DEBUG("   - Pos time support: {}", capabilities & (1 << 5) ? "yes" : "no");
    DEBUG("   - Chapter support:  {}", capabilities & (1 << 6) ? "yes" : "no");

    *videoResult = "mpeg dash";

    return videoResult;
}

bool StreamHandler::createVideoWithLength(std::string seconds, const std::string& name) {
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
        // "ffmpeg", "-hide_banner" , "-y", "-i", "pipe:0", "-t", seconds, "-codec", "copy", "-f", "webm", "pipe:1"
        "ffmpeg", "-hide_banner" , "-y", "-i", movie_path + "/transparent-full.webm", "-t", seconds, "-codec", "copy", "-f", "webm", "pipe:1"
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

    // process.write(transparent_movie);

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

std::string StreamHandler::getAudioInfo() {
    if (mse) {
        return mse->audioInfo();
    }

    json result;
    result["stream"] = json::array();
    result["subtitle"] = json::array();

    auto tag = [](const json& v, const char* name) -> std::string {
        if (v.contains("tags") && v["tags"].is_object() && v["tags"].contains(name) && v["tags"][name].is_string()) {
            return v["tags"][name];
        }
        return "";
    };

    auto disposition = [](const json& v, const char* name) -> bool {
        return v.contains("disposition") && v["disposition"].is_object() && v["disposition"].value(name, 0) != 0;
    };

    // The index counts the audio (subtitle) streams in the order of the TS. VDR selects
    // the track with the same index in the PID ordered track list.
    int audioIndex = 0;
    int subtitleIndex = 0;
    for (auto& v : bstreams) {
        if (!v.is_object()) {
            continue;
        }

        std::string type = v.value("codec_type", "");

        if (type == "audio") {
            json lang;
            lang["index"] = audioIndex++;
            lang["language"] = tag(v, "language");
            lang["comment"] = !tag(v, "comment").empty() ? tag(v, "comment") : tag(v, "handler_name");
            lang["codec"] = v.value("codec_name", "");
            lang["channels"] = v.value("channels", 0);
            lang["audioDescription"] = disposition(v, "visual_impaired");

            result["stream"].push_back(lang);
        } else if (type == "subtitle" && v.value("codec_name", "") == "dvb_subtitle") {
            json sub;
            sub["index"] = subtitleIndex++;
            sub["language"] = tag(v, "language");
            sub["hearingImpaired"] = disposition(v, "hearing_impaired");

            result["subtitle"].push_back(sub);
        }
    }

    return result.dump();
}

std::shared_ptr<std::string> StreamHandler::mseStart(const std::string& postfix, const std::string& duration, const std::vector<std::string>& tracks) {
    this->postfix = postfix;

    stopVideo();
    stopRequest = false;
    streamError = false;

    // the transparent video of the browser has the duration of the MSE stream
    transparentVideoFile = "transparent-video-" + browserIp + "_" + std::to_string(browserPort) + "-" + this->postfix + ".webm";

    std::string length = "N/A";
    try {
        double d = std::stod(duration);
        if (d > 0 && d < 100000) {
            length = std::to_string(d);
        }
    } catch (...) {
        // live stream or unknown duration
    }

    if (!createVideoWithLength(length, transparentVideoFile)) {
        return nullptr;
    }

    // audio infos are taken from the init segments
    bstreams = json::array();

    mse = std::make_unique<MseRemuxer>(vdrClient, tracks);

    std::string info = "mse";
    for (const auto& t : tracks) {
        info += " " + t;
    }

    DEBUG("MSE start: {}", info);

    return std::make_shared<std::string>(info);
}

bool StreamHandler::mseAppend(int track, const std::string& data) {
    return mse && mse->append(track, data);
}

bool StreamHandler::mseReset() {
    if (!mse) {
        return false;
    }

    mse->reset();
    return true;
}

bool StreamHandler::mseEndOfStream() {
    if (!mse) {
        return false;
    }

    mse->endOfStream();
    return true;
}
