#include "download.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <fcntl.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <csignal>
#include <ctime>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "json.hpp"
#include "process.hpp"
#include "logger.h"
#include "streamhandler.h"

using json = nlohmann::json;
namespace fs = std::filesystem;
using namespace remotetranscoder;

namespace downloads {

namespace {

struct Download {
    std::string id;
    std::string name;                   // file name
    std::string state;                  // running, failed
    std::string error;
    double duration = 0;
    int progress = 0;
    pid_t pid = 0;
    bool cancelled = false;
    bool interrupted = false;           // stopped by the end of the program
    bool noSpace = false;               // stopped, the disk is full
    std::string convertFrom;            // the MP4 file of a conversion, deleted at the end
    std::chrono::steady_clock::time_point cancelTime;
    DownloadStartType params;           // to start it again, the data of a recording
    DownloadSource source;
};

std::mutex mutex;
std::condition_variable finished;
std::string directory;
std::string mediathekUrl;
std::map<std::string, std::shared_ptr<Download>> active;     // running and failed
int nextId = 1;
int running = 0;
int maxParallel = 0;                    // downloads at the same time, 0: no limit
bool stopping = false;                  // no more downloads are started
DownloadSource lastSource;

const int durationTolerance = 3;        // seconds
const size_t maxCandidates = 10;
const size_t maxProbes = 6;             // entries whose quality is probed

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string formatTime(time_t t, const char *format) {
    struct tm tm {};
    localtime_r(&t, &tm);

    char buf[64];
    strftime(buf, sizeof(buf), format, &tm);
    return buf;
}

// a name from foreign data as a file name: no path, no characters other systems
// (Samba, Windows) can't use, not hidden and not too long
std::string fileName(const std::string& name) {
    std::string result;
    for (unsigned char c : name) {
        if (c == ':') {
            result += " -";             // "Terra X: Folge" -> "Terra X - Folge"
        } else if (c == '/' || c == '\\') {
            result += '-';              // "Folge 1/3" -> "Folge 1-3"
        } else if (c == '"') {
            result += '\'';
        } else if (c < 0x20 || std::string("*?<>|").find((char) c) != std::string::npos) {
            result += ' ';
        } else {
            result += (char) c;
        }
    }

    // collapse spaces, trim spaces and dots
    std::string collapsed;
    for (char c : result) {
        if (c != ' ' || (!collapsed.empty() && collapsed.back() != ' ')) {
            collapsed += c;
        }
    }
    while (!collapsed.empty() && (collapsed.back() == ' ' || collapsed.back() == '.')) {
        collapsed.pop_back();
    }
    while (!collapsed.empty() && (collapsed.front() == ' ' || collapsed.front() == '.')) {
        collapsed.erase(0, 1);
    }

    // at most 200 bytes, don't cut an UTF-8 character
    if (collapsed.size() > 200) {
        size_t n = 200;
        while (n > 0 && ((unsigned char) collapsed[n] & 0xC0) == 0x80) {
            --n;
        }
        collapsed.resize(n);
    }

    return collapsed.empty() ? "Video" : collapsed;
}

// MediathekView channel of a VDR channel and whether it belongs to the ARD.
// An ARD application also plays the videos of the other ARD broadcasters.
struct Channel {
    std::string name;
    bool ard = false;
};

Channel mediathekChannel(const std::string& vdrChannel) {
    static const std::vector<std::pair<std::string, Channel>> channels = {
        { "3sat",           { "3Sat" } },
        { "arte",           { "ARTE.DE" } },
        { "phoenix",        { "PHOENIX" } },
        { "kika",           { "KiKA" } },
        { "zdf",            { "ZDF" } },
        { "das erste",      { "ARD", true } },
        { "tagesschau24",   { "ARD", true } },
        { "ard",            { "ARD", true } },
        { "one",            { "ARD", true } },
        { "br ",            { "BR", true } },
        { "bayerisches",    { "BR", true } },
        { "hr-fernsehen",   { "HR", true } },
        { "hr fernsehen",   { "HR", true } },
        { "mdr",            { "MDR", true } },
        { "ndr",            { "NDR", true } },
        { "rbb",            { "RBB", true } },
        { "sr fernsehen",   { "SR", true } },
        { "swr",            { "SWR", true } },
        { "wdr",            { "WDR", true } },
        { "radio bremen",   { "Radio Bremen TV", true } },
        { "orf",            { "ORF" } },
        { "srf",            { "SRF" } },
    };

    std::string name = lower(vdrChannel) + " ";
    for (const auto& c : channels) {
        if (name.find(c.first) != std::string::npos) {
            return c.second;
        }
    }

    return {};
}

bool isArd(const std::string& channel) {
    static const std::set<std::string> ard = { "ard", "br", "hr", "mdr", "ndr", "rbb", "sr", "swr", "wdr", "radio bremen tv", "ard-alpha", "tagesschau24", "one" };
    return ard.count(lower(channel)) > 0;
}

// MediathekView stores the other qualities as "<length of the common prefix>|<rest>"
std::string expandUrl(const std::string& url, const std::string& base) {
    size_t bar = url.find('|');
    if (bar == std::string::npos || bar == 0 || url.find_first_not_of("0123456789") != bar) {
        return url;
    }

    size_t prefix = std::stoul(url.substr(0, bar));
    if (prefix > base.size()) {
        return url;
    }

    return base.substr(0, prefix) + url.substr(bar + 1);
}

// IDs in a url: alphanumeric parts with a digit and at least 6 characters
std::set<std::string> urlIds(const std::string& url) {
    std::set<std::string> ids;
    std::string part;

    auto add = [&]() {
        if (part.size() >= 6 && part.find_first_of("0123456789") != std::string::npos) {
            ids.insert(lower(part));
        }
        part.clear();
    };

    for (char c : url) {
        if (std::isalnum((unsigned char) c)) {
            part += c;
        } else {
            add();
        }
    }
    add();

    return ids;
}

// IDs that say nothing about the video: resolutions, bitrates, short numbers
bool genericId(const std::string& id) {
    bool digits = id.find_first_not_of("0123456789") == std::string::npos;
    if (digits) {
        return id.size() < 8;
    }

    // 1920x1080, 1080p50, 3500k, 128kbit
    size_t i = 0;
    while (i < id.size() && std::isdigit((unsigned char) id[i])) {
        ++i;
    }
    std::string rest = id.substr(i);
    if (i > 0 && (rest == "p" || rest == "k" || rest == "kbit" || rest == "kbps")) {
        return true;
    }
    if (i > 0 && rest.size() > 1 && rest[0] == 'x' && rest.find_first_not_of("0123456789", 1) == std::string::npos) {
        return true;
    }
    if (i > 0 && rest.size() > 1 && rest[0] == 'p' && rest.find_first_not_of("0123456789", 1) == std::string::npos) {
        return true;
    }

    return false;
}

// lower case words of at least 4 letters, UTF-8 characters count as letters
std::vector<std::string> words(const std::string& text) {
    std::vector<std::string> result;
    std::string word;

    auto add = [&]() {
        if (word.size() >= 4) {
            result.push_back(lower(word));
        }
        word.clear();
    };

    for (unsigned char c : text) {
        if (std::isalnum(c) || c >= 0x80) {
            word += (char) c;
        } else {
            add();
        }
    }
    add();

    return result;
}

// Other versions of a broadcast, they are listed after the normal version.
// The name is translated by VDR.
std::string variant(const std::string& topic, const std::string& title) {
    static const std::vector<std::pair<const char *, const char *>> variants = {
        { "audiodeskription",     "audio description" },
        { "(ad)",                 "audio description" },
        { "hörfassung",           "audio description" },
        { "gebärdensprache",      "sign language" },
        { "(dgs)",                "sign language" },
        { "originalversion",      "original version" },
        { "originalfassung",      "original version" },
        { "(ov)",                 "original version" },
        { "(omu)",                "original version" },
        { "klare sprache",        "plain language" },
        { "leichte sprache",      "plain language" },
        { "trailer",              "trailer" },
    };

    std::string text = lower(topic + " " + title);
    for (const auto& v : variants) {
        if (text.find(v.first) != std::string::npos) {
            return v.second;
        }
    }

    return "";
}

// numbers of the API may also be strings or null
int64_t number(const json& e, const char *key) {
    auto it = e.find(key);
    if (it == e.end()) {
        return 0;
    }
    if (it->is_number()) {
        return it->get<int64_t>();
    }
    if (it->is_string()) {
        try {
            return std::stoll(it->get<std::string>());
        } catch (...) {
        }
    }
    return 0;
}

std::string text(const json& e, const char *key) {
    auto it = e.find(key);
    return it != e.end() && it->is_string() ? it->get<std::string>() : "";
}

json query(const json& body) {
    httplib::Client client(mediathekUrl);
    client.set_connection_timeout(5);
    client.set_read_timeout(10);
    client.set_follow_location(true);

    // the API expects text/plain, application/json fails
    auto res = client.Post("/api/query", body.dump(), "text/plain");
    if (!res) {
        throw std::runtime_error("no connection: " + httplib::to_string(res.error()));
    }
    if (res->status != 200) {
        throw std::runtime_error("status " + std::to_string(res->status));
    }

    auto j = json::parse(res->body);
    if (j.contains("err") && !j["err"].is_null()) {
        throw std::runtime_error(j["err"].dump());
    }

    return j.at("result").at("results");
}

json queryBody(const std::string& channel, int duration, bool withDuration, int size) {
    json body;
    body["queries"] = json::array();
    if (!channel.empty()) {
        body["queries"].push_back({ { "fields", { "channel" } }, { "query", channel } });
    }
    if (withDuration) {
        body["duration_min"] = std::max(0, duration - durationTolerance);
        body["duration_max"] = duration + durationTolerance;
    }
    body["sortBy"] = "timestamp";
    body["sortOrder"] = "desc";
    body["future"] = false;
    body["offset"] = 0;
    body["size"] = size;
    return body;
}

std::string candidateName(const std::string& channel, const std::string& topic, const std::string& title, int64_t timestamp) {
    std::string name = channel + " - ";

    // the title often repeats the topic
    if (!topic.empty() && lower(title).find(lower(topic)) == std::string::npos) {
        name += topic + " - ";
    }

    name += title;
    if (timestamp > 0) {
        name += " (" + formatTime((time_t) timestamp, "%Y-%m-%d") + ")";
    }

    return fileName(name);
}

// a name for the file that isn't used yet, called with the mutex locked
std::string uniqueName(const std::string& name, const std::string& extension) {
    std::string base = fileName(name);

    for (int i = 1; ; ++i) {
        std::string candidate = i == 1 ? base : base + " (" + std::to_string(i) + ")";
        std::string file = candidate + extension;

        bool used = fs::exists(directory + "/" + file) || fs::exists(directory + "/" + file + ".part");
        for (const auto& d : active) {
            used = used || d.second->name == file;
        }

        if (!used) {
            return file;
        }
    }
}

// a downloaded file of the directory
bool validFile(const std::string& file) {
    return !file.empty() && file[0] != '.' && file.find('/') == std::string::npos && (endsWith(lower(file), ".mp4") || endsWith(lower(file), ".ts"));
}

// CIFS and NFS may deny root what the mode bits allow: try it
bool writable(const std::string& dir) {
    std::string test = dir + "/.remotetranscode-write-test";

    int fd = ::open(test.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        ERROR("Download directory {} is not writable: {}", dir, strerror(errno));
        return false;
    }

    ::close(fd);
    ::unlink(test.c_str());
    return true;
}

// free bytes of the file system, -1: unknown
int64_t freeBytes(const std::string& dir) {
    struct statvfs st {};
    if (statvfs(dir.c_str(), &st) != 0) {
        return -1;
    }
    return (int64_t) st.f_bavail * (int64_t) st.f_frsize;
}

// The data of a download in a hidden file next to it: the information of a
// finished file, and a failed or interrupted download can be started again.
std::string sidecar(const std::string& file) {
    return directory + "/." + file + ".json";
}

void writeSidecar(const Download& d, const std::string& state) {
    json j;
    j["state"] = state;
    j["error"] = d.error;
    j["format"] = d.params.format;
    j["duration"] = d.duration;
    j["maxRate"] = d.params.maxRate;
    j["topic"] = d.params.topic;
    j["title"] = d.params.title;
    j["channel"] = d.params.channel;
    j["timestamp"] = d.params.timestamp;
    j["description"] = d.params.description;
    j["width"] = d.params.width;
    j["height"] = d.params.height;
    j["url"] = d.source.url;
    j["cookies"] = d.source.cookies;
    j["referer"] = d.source.referer;
    j["userAgent"] = d.source.userAgent;
    j["maps"] = d.source.maps;
    j["convertFrom"] = d.convertFrom;

    std::string file = sidecar(d.name);
    FILE *f = fopen((file + ".tmp").c_str(), "w");
    if (f == nullptr) {
        ERROR("Unable to write {}: {}", file, strerror(errno));
        return;
    }
    std::string text = j.dump(2);
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = fclose(f) == 0 && ok;

    std::error_code ec;
    if (ok) {
        fs::rename(file + ".tmp", file, ec);
    } else {
        fs::remove(file + ".tmp", ec);
    }
}

// the download of a file with its sidecar, the state of the sidecar
std::shared_ptr<Download> readSidecar(const std::string& file, std::string& state) {
    FILE *f = fopen(sidecar(file).c_str(), "r");
    if (f == nullptr) {
        return nullptr;
    }

    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        text.append(buf, n);
    }
    fclose(f);

    try {
        auto j = json::parse(text);
        auto d = std::make_shared<Download>();
        d->name = file;
        state = j.value("state", "");
        d->error = j.value("error", "");
        d->duration = j.value("duration", 0.0);
        d->params.name = file;
        d->params.format = j.value("format", "");
        d->params.duration = (int) d->duration;
        d->params.maxRate = j.value("maxRate", 0);
        d->params.topic = j.value("topic", "");
        d->params.title = j.value("title", "");
        d->params.channel = j.value("channel", "");
        d->params.timestamp = j.value("timestamp", (int64_t) 0);
        d->params.description = j.value("description", "");
        d->params.width = j.value("width", 0);
        d->params.height = j.value("height", 0);
        d->source.url = j.value("url", "");
        d->source.cookies = j.value("cookies", "");
        d->source.referer = j.value("referer", "");
        d->source.userAgent = j.value("userAgent", "");
        d->source.maps = j.value("maps", std::vector<std::string>());
        d->source.duration = d->duration;
        d->convertFrom = j.value("convertFrom", "");
        return d;
    } catch (const std::exception& e) {
        ERROR("Unable to read {}: {}", sidecar(file), e.what());
        return nullptr;
    }
}

void finish(const std::shared_ptr<Download>& d, int exitStatus, const std::string& lastError) {
    std::string part = directory + "/" + d->name + ".part";
    std::error_code ec;

    std::lock_guard<std::mutex> lock(mutex);

    if (d->noSpace) {
        ERROR("Download {}: not enough disk space", d->name);
        fs::remove(part, ec);
        d->state = "failed";
        d->error = "Not enough disk space";
        writeSidecar(*d, "failed");
    } else if (d->interrupted) {
        // started again at the next start of the program
        INFO("Download {} interrupted", d->name);
        fs::remove(part, ec);
        d->state = "failed";
        d->error = "Interrupted";
        writeSidecar(*d, "failed");
    } else if (d->cancelled) {
        INFO("Download {} cancelled", d->name);
        fs::remove(part, ec);
        fs::remove(sidecar(d->name), ec);
        active.erase(d->id);
    } else if (exitStatus == 0) {
        fs::rename(part, directory + "/" + d->name, ec);
        if (ec) {
            ERROR("Download {}: unable to rename the file: {}", d->name, ec.message());
            d->state = "failed";
            d->error = ec.message();
            writeSidecar(*d, "failed");
        } else {
            INFO("{} {} finished", d->convertFrom.empty() ? "Download" : "Conversion", d->name);
            d->progress = 100;
            writeSidecar(*d, "done");
            active.erase(d->id);

            if (!d->convertFrom.empty()) {
                fs::remove(directory + "/" + d->convertFrom, ec);
                fs::remove(sidecar(d->convertFrom), ec);
            }
        }
    } else {
        ERROR("Download {} failed with exit code {}: {}", d->name, exitStatus, lastError);
        fs::remove(part, ec);
        d->state = "failed";
        d->error = lastError.empty() ? "ffmpeg exit code " + std::to_string(exitStatus) : lastError;
        writeSidecar(*d, "failed");
    }

    --running;
    finished.notify_all();
}

void startQueued();

// referer, user agent, cookies and timeouts of the input, for ffmpeg and ffprobe
std::vector<std::string> inputOptions(const DownloadSource& source) {
    std::vector<std::string> options;

    if (!source.referer.empty()) {
        options.emplace_back("-referer");
        options.emplace_back(source.referer);
    }
    if (!source.userAgent.empty()) {
        options.emplace_back("-user_agent");
        options.emplace_back(source.userAgent);
    }
    if (!source.cookies.empty()) {
        options.emplace_back("-headers");
        options.emplace_back("Cookie: " + source.cookies);
    }

    addNetworkOptions(options, source.url);

    // a download survives a longer interruption of the network than the playback
    if (source.url.rfind("http://", 0) == 0 || source.url.rfind("https://", 0) == 0) {
        options.insert(options.end(), { "-reconnect_on_network_error", "1", "-reconnect_delay_max", "30" });
    }

    return options;
}

// the duration in seconds, 0: unknown or live
double probeDuration(const DownloadSource& source) {
    std::vector<std::string> cmd = { "ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0" };
    auto options = inputOptions(source);
    cmd.insert(cmd.end(), options.begin(), options.end());
    cmd.emplace_back(source.url);

    std::string out;
    TinyProcessLib::Process process(cmd, "", [&](const char *bytes, size_t n) { out.append(bytes, n); }, [](const char *, size_t) {});
    if (process.get_exit_status() != 0) {
        return 0;
    }

    try {
        return std::stod(out);
    } catch (...) {
        return 0;       // N/A
    }
}

struct VideoInfo {
    bool ok = false;
    int width = 0;
    int height = 0;
    int64_t bitrate = 0;                // bit/s, 0: unknown
    std::string map;                    // HLS: the chosen variant
};

// The size and bitrate of the video that is downloaded. HLS: the variant of the
// map, else the best variant up to maxHeight (0: the best).
VideoInfo videoInfo(const DownloadSource& source, int maxHeight) {
    VideoInfo info;

    std::vector<std::string> cmd = { "ffprobe", "-v", "error", "-print_format", "json", "-show_entries",
                                     "program=program_id:program_tags=variant_bitrate:stream=index,codec_type,width,height,bit_rate:format=bit_rate" };
    auto options = inputOptions(source);
    cmd.insert(cmd.end(), options.begin(), options.end());
    cmd.emplace_back(source.url);

    std::string out;
    TinyProcessLib::Process process(cmd, "", [&](const char *bytes, size_t n) { out.append(bytes, n); }, [](const char *, size_t) {});
    if (process.get_exit_status() != 0) {
        return info;
    }

    try {
        auto j = json::parse(out);
        auto programs = j.value("programs", json::array());

        int fixed = -1;
        for (const auto& m : source.maps) {
            if (m.rfind("0:p:", 0) == 0) {
                fixed = std::stoi(m.substr(4));
            }
        }

        if (programs.size() > 1 || fixed >= 0) {
            // HLS variants: the best that fits, else the smallest
            const json *best = nullptr;
            const json *smallest = nullptr;
            int bestHeight = -1, smallestHeight = INT32_MAX;
            int64_t bestRate = -1;

            for (const auto& p : programs) {
                int height = 0;
                for (const auto& st : p.value("streams", json::array())) {
                    if (text(st, "codec_type") == "video") {
                        height = std::max(height, (int) number(st, "height"));
                    }
                }
                int64_t rate = p.contains("tags") ? number(p["tags"], "variant_bitrate") : 0;

                if (fixed >= 0) {
                    if (number(p, "program_id") == fixed) {
                        best = &p;
                    }
                    continue;
                }
                if ((maxHeight <= 0 || height <= maxHeight) && (height > bestHeight || (height == bestHeight && rate > bestRate))) {
                    best = &p;
                    bestHeight = height;
                    bestRate = rate;
                }
                if (height < smallestHeight) {
                    smallest = &p;
                    smallestHeight = height;
                }
            }

            const json *p = best != nullptr ? best : smallest;
            if (p == nullptr) {
                return info;
            }

            for (const auto& st : p->value("streams", json::array())) {
                if (text(st, "codec_type") == "video" && number(st, "height") > info.height) {
                    info.width = (int) number(st, "width");
                    info.height = (int) number(st, "height");
                }
            }
            info.bitrate = p->contains("tags") ? number((*p)["tags"], "variant_bitrate") : 0;
            info.map = "0:p:" + std::to_string(number(*p, "program_id"));
        } else {
            // the streams of the maps, without maps the best video
            int64_t sum = 0;
            for (const auto& st : j.value("streams", json::array())) {
                std::string map = "0:" + std::to_string(number(st, "index"));
                if (!source.maps.empty() && std::find(source.maps.begin(), source.maps.end(), map) == source.maps.end()) {
                    continue;
                }

                sum += number(st, "bit_rate");
                if (text(st, "codec_type") == "video" && number(st, "height") > info.height) {
                    info.width = (int) number(st, "width");
                    info.height = (int) number(st, "height");
                }
            }

            // HLS with one variant: its bitrate, the bitrate of the format is useless
            int64_t variant = programs.size() == 1 && programs[0].contains("tags") ? number(programs[0]["tags"], "variant_bitrate") : 0;
            info.bitrate = variant > 0 ? variant : source.maps.empty() && j.contains("format") ? number(j["format"], "bit_rate") : sum;
        }

        // less is no video
        if (info.bitrate < 10000) {
            info.bitrate = 0;
        }
        info.ok = info.height > 0 || info.bitrate > 0;
    } catch (const std::exception& e) {
        ERROR("Unable to read the video information: {}", e.what());
    }

    return info;
}

// the bitrate of the downloaded streams in bit/s, 0: unknown
int64_t bitrate(const DownloadSource& source) {
    return videoInfo(source, 0).bitrate;
}

void run(const std::shared_ptr<Download>& d, std::vector<std::string> cmd) {
    const DownloadSource& source = d->source;
    int maxRate = d->params.maxRate;

    std::string out;            // incomplete line of the progress
    std::string lastError;
    std::mutex errorMutex;

    // ffmpeg limits the speed only as multiple of the playback: the factor from the bitrate
    if (maxRate > 0) {
        int64_t rate = bitrate(source);
        if (rate > 0) {
            double factor = std::max(0.1, maxRate * 1000.0 / rate);
            INFO("Download {}: {} kbit/s, limit {} kbit/s, read at {:.1f}x", d->name, rate / 1000, maxRate, factor);

            auto input = std::find(cmd.begin(), cmd.end(), "-i");
            cmd.insert(input, { "-readrate", std::to_string(factor) });
        } else {
            ERROR("Download {}: unknown bitrate, no limit", d->name);
        }
    }

    TinyProcessLib::Process process(cmd, "",
        [&](const char *bytes, size_t n) {
            out.append(bytes, n);

            size_t eol;
            while ((eol = out.find('\n')) != std::string::npos) {
                std::string line = out.substr(0, eol);
                out.erase(0, eol + 1);

                // microseconds, despite its name also in out_time_ms
                if (line.rfind("out_time_us=", 0) == 0 || line.rfind("out_time_ms=", 0) == 0) {
                    try {
                        double seconds = std::stod(line.substr(12)) / 1000000;
                        std::lock_guard<std::mutex> lock(mutex);
                        if (d->duration > 0) {
                            d->progress = std::clamp((int) (seconds * 100 / d->duration), 0, 99);
                        }
                    } catch (...) {
                        // N/A at the start
                    }
                }
            }
        },
        [&](const char *bytes, size_t n) {
            std::string msg(bytes, n);
            while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
                msg.pop_back();
            }

            if (!msg.empty()) {
                std::lock_guard<std::mutex> lock(errorMutex);
                lastError = msg.substr(msg.rfind('\n') == std::string::npos ? 0 : msg.rfind('\n') + 1);
            }
        },
        true);

    if (process.get_id() <= 0) {
        finish(d, -1, "ffmpeg can't be started");
        startQueued();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        d->pid = process.get_id();

        // cancelled before the process was started
        if (d->cancelled) {
            ::kill(d->pid, SIGTERM);
        }
    }

    // a cancelled download gets SIGTERM, if ffmpeg doesn't stop SIGKILL
    int exitStatus;
    bool killed = false;
    int64_t reserve = (int64_t) d->params.reserve * 1024 * 1024;
    auto lastCheck = std::chrono::steady_clock::now();

    while (!process.try_get_exit_status(exitStatus)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // the disk becomes full: stop
        int64_t free = -1;
        if (reserve > 0 && std::chrono::steady_clock::now() - lastCheck > std::chrono::seconds(5)) {
            lastCheck = std::chrono::steady_clock::now();
            free = freeBytes(directory);
        }

        std::lock_guard<std::mutex> lock(mutex);
        if (free >= 0 && free < reserve && !d->cancelled) {
            d->noSpace = true;
            d->cancelled = true;
            d->cancelTime = std::chrono::steady_clock::now();
            ::kill(d->pid, SIGTERM);
            ::kill(-d->pid, SIGTERM);
        }
        if (d->cancelled && !killed && std::chrono::steady_clock::now() - d->cancelTime > std::chrono::seconds(5)) {
            ::kill(-d->pid, SIGKILL);
            ::kill(d->pid, SIGKILL);
            killed = true;
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        d->pid = 0;
    }

    std::string error;
    {
        std::lock_guard<std::mutex> lock(errorMutex);
        error = lastError;
    }

    finish(d, exitStatus, error);
    startQueued();
}

// the part file is created at once: a failure is reported now, not later in the list
std::string launch(const std::shared_ptr<Download>& d) {
    const DownloadSource& source = d->source;

    std::vector<std::string> cmd = { "ffmpeg", "-hide_banner", "-nostdin", "-nostats", "-loglevel", "error", "-y" };
    auto options = inputOptions(source);
    cmd.insert(cmd.end(), options.begin(), options.end());
    cmd.emplace_back("-i");
    cmd.emplace_back(source.url);

    // the streams of the playback, else ffmpeg chooses the best video and audio
    for (const auto& m : source.maps) {
        cmd.emplace_back("-map");
        cmd.emplace_back(m);
    }

    if (d->params.format == "ts") {
        cmd.insert(cmd.end(), { "-c", "copy", "-f", "mpegts", "-progress", "pipe:1" });
    } else {
        cmd.insert(cmd.end(), { "-c", "copy", "-f", "mp4", "-movflags", "+faststart", "-progress", "pipe:1" });
    }

    // the reserve and the estimated size must be free, a conversion is checked before
    if (d->params.reserve > 0 && d->convertFrom.empty()) {
        int64_t estimate = bitrate(source) * (int64_t) d->duration / 8;
        int64_t free = freeBytes(directory);
        if (free >= 0 && free < (int64_t) d->params.reserve * 1024 * 1024 + estimate) {
            ERROR("Download {}: {} MB free, {} MB needed", d->name, free / 1048576, d->params.reserve + estimate / 1048576);
            return "Not enough disk space";
        }
    }

    std::string part = directory + "/" + d->name + ".part";
    int fd = ::open(part.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (fd < 0) {
        ERROR("Unable to create {}: {}", part, strerror(errno));
        return "The download directory is not writable";
    }
    ::close(fd);

    {
        std::lock_guard<std::mutex> lock(mutex);

        // cancelled while it started
        if (d->cancelled || stopping) {
            std::error_code ec;
            fs::remove(part, ec);
            if (d->cancelled) {
                fs::remove(sidecar(d->name), ec);
                active.erase(d->id);
            } else {
                d->state = "failed";
                d->error = "Interrupted";
                writeSidecar(*d, "failed");
            }
            return "";
        }

        d->state = d->convertFrom.empty() ? "running" : "converting";
        d->error.clear();
        d->progress = 0;
        d->cancelled = false;
        d->noSpace = false;
        writeSidecar(*d, "running");
        ++running;
    }

    cmd.emplace_back(part);
    INFO("{} {} from {}", d->convertFrom.empty() ? "Download" : "Conversion", d->name, source.url);

    std::thread(run, d, cmd).detach();
    return "";
}

// queued downloads, as many as may run at the same time
void startQueued() {
    std::vector<std::shared_ptr<Download>> next;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopping) {
            return;
        }

        std::vector<std::shared_ptr<Download>> queued;
        int starting = 0;
        for (const auto& kv : active) {
            if (kv.second->state == "queued") {
                queued.push_back(kv.second);
            } else if (kv.second->state == "starting") {
                ++starting;
            }
        }

        std::sort(queued.begin(), queued.end(), [](const auto& a, const auto& b) { return std::stoi(a->id) < std::stoi(b->id); });

        for (const auto& d : queued) {
            if (maxParallel > 0 && running + starting >= maxParallel) {
                break;
            }
            d->state = "starting";
            ++starting;
            next.push_back(d);
        }
    }

    for (const auto& d : next) {
        std::string error = launch(d);
        if (!error.empty()) {
            std::lock_guard<std::mutex> lock(mutex);
            d->state = "failed";
            d->error = error;
            writeSidecar(*d, "failed");
        }
    }
}

}

void setup(const std::string& dir, const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex);

    directory = dir;
    while (directory.size() > 1 && directory.back() == '/') {
        directory.pop_back();
    }
    mediathekUrl = url;

    if (directory.empty()) {
        return;
    }

    std::error_code ec;
    fs::create_directories(directory, ec);
    if (ec) {
        ERROR("Unable to create the download directory {}: {}", directory, ec.message());
    }

    // Downloads of the last run: an interrupted download is listed as failed.
    // A part file without data is left over by a crash or power loss.
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        std::string file = entry.path().filename().string();
        if (file.size() > 6 && file[0] == '.' && endsWith(file, ".json")) {
            std::string name = file.substr(1, file.size() - 6);
            std::string state;
            auto d = readSidecar(name, state);
            if (d == nullptr || (state == "done" && !fs::exists(directory + "/" + name, ec))) {
                fs::remove(entry.path(), ec);
            } else if (state == "running" || state == "queued" || state == "failed") {
                INFO("Download {}: {}", name, state == "failed" ? d->error : "interrupted");
                d->id = std::to_string(nextId++);
                d->state = "failed";
                d->error = state == "failed" ? d->error : "Interrupted";
                writeSidecar(*d, "failed");
                active[d->id] = d;
            }
        }
    }

    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        if (entry.is_regular_file() && endsWith(entry.path().filename().string(), ".part")) {
            INFO("Remove the incomplete download {}", entry.path().string());
            fs::remove(entry.path(), ec);
        }
    }
}

DownloadLookupResult lookup(const DownloadLookupType& input, const DownloadSource& source) {
    const std::string& vdrChannel = input.channel;
    DownloadLookupResult result;
    result.stream = false;

    std::string dir;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (directory.empty()) {
            result.error = "No download directory";
            return result;
        }
        dir = directory;
        lastSource = source;
    }

    if (!writable(dir)) {
        result.error = "The download directory is not writable";
        return result;
    }

    // HLS: the probe of the playback doesn't read the duration
    DownloadSource src = source;
    if (src.duration <= 0 && !src.url.empty()) {
        src.duration = probeDuration(src);
        std::lock_guard<std::mutex> lock(mutex);
        lastSource.duration = src.duration;
    }

    int duration = (int) std::lround(src.duration);
    if (duration <= 0) {
        result.error = "No video or a live stream";
        return result;
    }

    result.stream = !source.url.empty();
    result.streamName = fileName(vdrChannel + " - " + formatTime(time(nullptr), "%Y-%m-%d %H-%M"));

    // the stream: HLS in the quality of the setup
    if (result.stream) {
        VideoInfo v = videoInfo(src, input.maxHeight);
        result.streamWidth = v.width;
        result.streamHeight = v.height;
        result.streamSize = v.bitrate * duration / 8;

        if (!v.map.empty()) {
            std::lock_guard<std::mutex> lock(mutex);
            lastSource.maps = { v.map };
        }
    }

    Channel channel = mediathekChannel(vdrChannel);
    std::map<std::string, json> entries;

    auto add = [&](const json& results) {
        for (const auto& e : results) {
            int64_t d = number(e, "duration");
            std::string id = text(e, "id");
            if (!id.empty() && d > 0 && std::abs(d - duration) <= durationTolerance) {
                entries[id] = e;
            }
        }
    };

    try {
        if (!channel.name.empty()) {
            add(query(queryBody(channel.name, duration, true, 50)));
        }

        // an ARD application also plays the videos of the other ARD broadcasters
        if (channel.name.empty() || channel.ard) {
            add(query(queryBody("", duration, true, 50)));
        }

        // the duration filter of the API is said to miss some entries
        if (entries.empty() && !channel.name.empty()) {
            add(query(queryBody(channel.name, duration, false, 500)));
        }
    } catch (const std::exception& e) {
        ERROR("MediathekView query failed: {}", e.what());
        result.error = "MediathekView is not reachable";
        return result;
    }

    // IDs of the stream, then the IDs of each entry
    std::set<std::string> ids;
    for (const auto& id : urlIds(source.url)) {
        if (!genericId(id)) {
            ids.insert(id);
        }
    }

    struct Entry {
        const json *e;
        std::string url;
        std::set<std::string> matched;      // IDs of the stream
    };

    // the same file is often listed by more ARD broadcasters: keep one,
    // the one of the channel of the application
    std::map<std::string, Entry> files;
    for (const auto& kv : entries) {
        const json& e = kv.second;

        std::string url = text(e, "url_video");
        std::string hd = expandUrl(text(e, "url_video_hd"), url);
        if (!hd.empty()) {
            url = hd;
        }
        if (url.empty()) {
            continue;
        }

        Entry entry { &e, url, {} };
        for (const auto& field : { "url_video", "url_video_hd", "url_video_low", "url_website" }) {
            for (const auto& id : urlIds(expandUrl(text(e, field), text(e, "url_video")))) {
                if (ids.count(id) > 0) {
                    entry.matched.insert(id);
                }
            }
        }

        auto it = files.find(url);
        if (it == files.end() || (!channel.name.empty() && lower(text(e, "channel")) == lower(channel.name))) {
            files[url] = entry;
        }
    }

    // an ID found in one entry only identifies the video
    std::map<std::string, int> idCount;
    for (const auto& f : files) {
        for (const auto& id : f.second.matched) {
            ++idCount[id];
        }
    }

    // words of the page title, without the names of the application
    std::set<std::string> titleWords;
    static const std::set<std::string> stopWords = {
        "mediathek", "hbbtv", "startseite", "video", "videos", "sendung", "sendungen", "folge", "folgen",
        "ganze", "live", "erste", "arte", "phoenix", "kika", "funk", "fernsehen", "programm", "suche"
    };
    for (const auto& w : words(input.pageTitle)) {
        if (stopWords.count(w) == 0 && lower(channel.name) != w) {
            titleWords.insert(w);
        }
    }

    struct Scored {
        double score;
        int64_t timestamp;
        DownloadCandidate candidate;
        std::vector<std::string> urls;      // HD, medium, low
        std::string medium;
        int64_t mediumSize;
    };
    std::vector<Scored> scored;

    for (const auto& f : files) {
        const json& e = *f.second.e;

        DownloadCandidate c;
        c.channel = text(e, "channel");
        c.topic = text(e, "topic");
        c.title = text(e, "title");
        c.duration = (int32_t) number(e, "duration");
        c.timestamp = number(e, "timestamp");
        c.url = f.second.url;
        c.name = candidateName(c.channel, c.topic, c.title, c.timestamp);
        c.description = text(e, "description");
        c.variant = variant(c.topic, c.title);
        c.exact = false;
        c.width = 0;
        c.height = 0;
        // the size of MediathekView belongs to the medium quality
        c.size = c.url == text(e, "url_video") ? number(e, "size") : 0;

        int delta = std::abs(c.duration - duration);
        double score = -5.0 * delta;
        if (!channel.name.empty() && lower(c.channel) == lower(channel.name)) {
            score += 20;
        } else if (channel.ard && isArd(c.channel)) {
            score += 10;
        }

        // the stream and the file of MediathekView often share an ID
        bool unique = false;
        for (const auto& id : f.second.matched) {
            unique = unique || idCount[id] == 1;
        }
        if (unique) {
            score += 100;
            c.exact = delta <= 2;
        } else if (!f.second.matched.empty()) {
            score += 10;
        }

        if (!c.variant.empty()) {
            score -= 30;
        }

        // the words of the page title in the topic or title
        std::string haystack = lower(c.topic + " " + c.title);
        int matches = 0;
        for (const auto& w : titleWords) {
            matches += haystack.find(w) != std::string::npos ? 1 : 0;
        }
        score += std::min(40, 10 * matches);

        // the broadcast in the EPG of the channel: only decides between otherwise
        // equal entries, an older video may be searched on purpose
        double epg = 0;
        for (const auto& ev : input.epg) {
            std::string t = lower(ev.title);
            if (t.size() < 4 || (lower(c.topic) != t && lower(c.title).find(t) == std::string::npos)) {
                continue;
            }
            bool sameTime = c.timestamp > 0 && std::abs(c.timestamp - ev.start) <= 3 * 3600;
            epg = std::max(epg, sameTime ? 3.0 : 2.0);
        }
        score += epg;

        std::vector<std::string> urls;
        std::string medium = text(e, "url_video");
        for (const auto& u : { expandUrl(text(e, "url_video_hd"), medium), medium, expandUrl(text(e, "url_video_low"), medium) }) {
            if (!u.empty() && std::find(urls.begin(), urls.end(), u) == urls.end()) {
                urls.push_back(u);
            }
        }

        scored.push_back({ score, c.timestamp, c, urls, medium, number(e, "size") });
    }

    std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
        return a.score != b.score ? a.score > b.score : a.timestamp > b.timestamp;
    });
    if (scored.size() > maxCandidates) {
        scored.resize(maxCandidates);
    }

    // The quality: the best up to maxHeight. The first entries are probed at the same
    // time, the others get the medium quality for SD.
    auto choose = [&](Scored& sc) {
        auto& c = sc.candidate;
        VideoInfo chosen;
        std::string chosenUrl;

        for (const auto& u : sc.urls) {
            DownloadSource s;
            s.url = u;
            VideoInfo v = videoInfo(s, 0);
            if (!v.ok) {
                continue;
            }

            chosen = v;
            chosenUrl = u;
            if (input.maxHeight <= 0 || v.height <= input.maxHeight) {
                break;
            }
        }

        if (!chosen.ok) {
            return;
        }

        c.url = chosenUrl;
        c.width = chosen.width;
        c.height = chosen.height;
        c.size = chosen.bitrate > 0 ? chosen.bitrate * c.duration / 8 : chosenUrl == sc.medium ? sc.mediumSize : 0;
    };

    std::vector<std::thread> probes;
    for (size_t i = 0; i < scored.size(); ++i) {
        if (i < maxProbes) {
            probes.emplace_back(choose, std::ref(scored[i]));
        } else if (input.maxHeight > 0 && input.maxHeight < 720 && !scored[i].medium.empty()) {
            scored[i].candidate.url = scored[i].medium;
            scored[i].candidate.size = scored[i].mediumSize;
        }
    }
    for (auto& t : probes) {
        t.join();
    }

    for (const auto& sc : scored) {
        result.candidates.push_back(sc.candidate);
    }

    INFO("Download lookup for {} ({} s): {} entries", vdrChannel, duration, result.candidates.size());
    return result;
}

std::string start(const DownloadStartType& input) {
    auto d = std::make_shared<Download>();
    d->params = input;
    if (d->params.format.empty()) {
        d->params.format = "mp4";
    }

    if (input.url.empty()) {
        std::lock_guard<std::mutex> lock(mutex);
        d->source = lastSource;
    } else {
        d->source.url = input.url;
        d->source.duration = input.duration;
    }

    if (d->source.url.rfind("http://", 0) != 0 && d->source.url.rfind("https://", 0) != 0) {
        return "This video can't be downloaded";
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        if (directory.empty()) {
            return "No download directory";
        }

        d->id = std::to_string(nextId++);
        d->name = uniqueName(input.name, d->params.format == "ts" ? ".ts" : ".mp4");
        d->duration = d->source.duration;
        active[d->id] = d;

        // more downloads wait
        maxParallel = std::max(0, input.maxParallel);
        int starting = 0;
        for (const auto& kv : active) {
            starting += kv.second->state == "starting" ? 1 : 0;
        }
        if (maxParallel > 0 && running + starting >= maxParallel) {
            INFO("Download {} queued", d->name);
            d->state = "queued";
            writeSidecar(*d, "queued");
            return "";
        }
        d->state = "starting";
    }

    std::string error = launch(d);
    if (!error.empty()) {
        std::lock_guard<std::mutex> lock(mutex);
        active.erase(d->id);
    }

    return error;
}

std::vector<DownloadInfo> list() {
    std::vector<DownloadInfo> result;
    std::error_code ec;

    std::lock_guard<std::mutex> lock(mutex);
    if (directory.empty()) {
        return result;
    }

    auto fill = [](DownloadInfo& info, const Download& d) {
        info.format = d.params.format.empty() ? "mp4" : d.params.format;
        info.topic = d.params.topic;
        info.title = d.params.title;
        info.channel = d.params.channel;
        info.timestamp = d.params.timestamp;
        info.description = d.params.description;
        info.duration = (int) d.duration;
        info.width = d.params.width;
        info.height = d.params.height;
    };

    for (const auto& kv : active) {
        const auto& d = kv.second;
        DownloadInfo info;
        info.id = d->id;
        info.name = d->name;
        info.state = d->state == "starting" ? "queued" : d->state;
        info.progress = d->progress;
        info.error = d->error;
        auto size = fs::file_size(directory + "/" + d->name + ".part", ec);
        info.size = ec ? 0 : (int64_t) size;
        fill(info, *d);
        result.push_back(info);
    }

    std::vector<std::pair<fs::file_time_type, DownloadInfo>> files;
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        std::string file = entry.path().filename().string();
        if (!entry.is_regular_file(ec) || !validFile(file)) {
            continue;
        }

        DownloadInfo info;
        info.id = "file:" + file;
        info.name = file;
        info.state = "done";
        info.progress = 100;
        info.path = entry.path().string();
        auto size = entry.file_size(ec);
        info.size = ec ? 0 : (int64_t) size;

        std::string state;
        auto d = readSidecar(file, state);
        if (d != nullptr) {
            fill(info, *d);
        }
        info.format = endsWith(lower(file), ".ts") ? "ts" : "mp4";

        files.emplace_back(entry.last_write_time(ec), info);
    }

    // the newest first
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& f : files) {
        result.push_back(f.second);
    }

    return result;
}

bool remove(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex);
    if (directory.empty()) {
        return false;
    }

    if (id.rfind("file:", 0) == 0) {
        std::string file = id.substr(5);
        if (!validFile(file)) {
            return false;
        }

        std::error_code ec;
        bool removed = fs::remove(directory + "/" + file, ec);
        INFO("Delete download {}: {}", file, removed ? "ok" : ec.message());
        fs::remove(sidecar(file), ec);
        return removed;
    }

    auto it = active.find(id);
    if (it == active.end()) {
        return false;
    }

    auto d = it->second;
    if (d->state == "running" || d->state == "converting" || d->state == "starting") {
        if (!d->cancelled) {
            d->cancelled = true;
            d->cancelTime = std::chrono::steady_clock::now();

            // the pid, not only the group: the child may not have its own group yet
            if (d->pid > 0) {
                ::kill(d->pid, SIGTERM);
                ::kill(-d->pid, SIGTERM);
            }
        }
    } else {
        std::error_code ec;
        fs::remove(sidecar(d->name), ec);
        active.erase(it);
    }

    return true;
}

std::string retry(const std::string& id) {
    std::shared_ptr<Download> d;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = active.find(id);
        if (it == active.end() || it->second->state != "failed") {
            return "This video can't be downloaded";
        }
        d = it->second;
        d->state = "queued";
        d->error.clear();
        d->cancelled = false;
        d->noSpace = false;
        d->interrupted = false;
        writeSidecar(*d, "queued");
    }

    // an error is shown in the list
    startQueued();
    return "";
}

std::string convert(const std::string& id, int reserve) {
    if (id.rfind("file:", 0) != 0) {
        return "This video can't be converted";
    }

    std::string file = id.substr(5);
    if (!validFile(file) || !endsWith(lower(file), ".mp4")) {
        return "This video can't be converted";
    }

    auto d = std::make_shared<Download>();
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (directory.empty()) {
            return "No download directory";
        }

        std::string path = directory + "/" + file;
        std::error_code ec;
        auto size = fs::file_size(path, ec);
        if (ec) {
            return "This video can't be converted";
        }

        // the TS file needs about the size of the MP4 file
        int64_t free = freeBytes(directory);
        if (free >= 0 && free < (int64_t) size * 105 / 100 + (int64_t) reserve * 1024 * 1024) {
            return "Not enough disk space";
        }

        // the data of the download, or an MP4 file of another source
        std::string state;
        auto old = readSidecar(file, state);
        if (old != nullptr) {
            d->params = old->params;
            d->duration = old->duration;
        } else {
            d->params.title = file.substr(0, file.size() - 4);
        }
        if (d->duration <= 0) {
            DownloadSource s;
            s.url = path;
            d->duration = probeDuration(s);
        }

        d->params.format = "ts";
        d->params.maxRate = 0;
        d->params.reserve = reserve;
        d->source.url = path;
        d->source.duration = d->duration;
        d->source.maps = { "0:v?", "0:a?" };
        d->convertFrom = file;
        d->id = std::to_string(nextId++);
        d->name = uniqueName(file.substr(0, file.size() - 4), ".ts");
        d->state = "starting";
        active[d->id] = d;
    }

    std::string error = launch(d);
    if (!error.empty()) {
        std::lock_guard<std::mutex> lock(mutex);
        active.erase(d->id);
    }

    return error;
}

void shutdown() {
    std::unique_lock<std::mutex> lock(mutex);
    stopping = true;

    for (const auto& kv : active) {
        const auto& d = kv.second;
        if ((d->state == "running" || d->state == "converting") && !d->cancelled) {
            d->cancelled = true;
            d->interrupted = true;
            d->cancelTime = std::chrono::steady_clock::now();
            if (d->pid > 0) {
                ::kill(d->pid, SIGTERM);
                ::kill(-d->pid, SIGTERM);
            }
        }
    }

    // the watcher kills ffmpeg after 5 s
    finished.wait_for(lock, std::chrono::seconds(10), [] { return running == 0; });
}

}
