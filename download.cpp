#include "download.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <fcntl.h>
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
    std::chrono::steady_clock::time_point cancelTime;
};

std::mutex mutex;
std::condition_variable finished;
std::string directory;
std::string mediathekUrl;
std::map<std::string, std::shared_ptr<Download>> active;     // running and failed
int nextId = 1;
int running = 0;
DownloadSource lastSource;

const int durationTolerance = 3;        // seconds
const size_t maxCandidates = 10;

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
std::string uniqueName(const std::string& name) {
    std::string base = fileName(name);

    for (int i = 1; ; ++i) {
        std::string candidate = i == 1 ? base : base + " (" + std::to_string(i) + ")";
        std::string file = candidate + ".mp4";

        bool used = fs::exists(directory + "/" + file) || fs::exists(directory + "/" + file + ".part");
        for (const auto& d : active) {
            used = used || d.second->name == file;
        }

        if (!used) {
            return file;
        }
    }
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

void finish(const std::shared_ptr<Download>& d, int exitStatus, const std::string& lastError) {
    std::string part = directory + "/" + d->name + ".part";
    std::error_code ec;

    std::lock_guard<std::mutex> lock(mutex);

    if (d->cancelled) {
        INFO("Download {} cancelled", d->name);
        fs::remove(part, ec);
        active.erase(d->id);
    } else if (exitStatus == 0) {
        fs::rename(part, directory + "/" + d->name, ec);
        if (ec) {
            ERROR("Download {}: unable to rename the file: {}", d->name, ec.message());
            d->state = "failed";
            d->error = ec.message();
        } else {
            INFO("Download {} finished", d->name);
            active.erase(d->id);
        }
    } else {
        ERROR("Download {} failed with exit code {}: {}", d->name, exitStatus, lastError);
        fs::remove(part, ec);
        d->state = "failed";
        d->error = lastError.empty() ? "ffmpeg exit code " + std::to_string(exitStatus) : lastError;
    }

    --running;
    finished.notify_all();
}

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
    return options;
}

// the bitrate of the downloaded streams in bit/s, 0: unknown
int64_t bitrate(const DownloadSource& source) {
    std::vector<std::string> cmd = { "ffprobe", "-v", "error", "-print_format", "json",
                                     "-show_entries", "format=bit_rate:stream=index,bit_rate:stream_tags=variant_bitrate" };
    auto options = inputOptions(source);
    cmd.insert(cmd.end(), options.begin(), options.end());
    cmd.emplace_back(source.url);

    std::string out;
    TinyProcessLib::Process process(cmd, "", [&](const char *bytes, size_t n) { out.append(bytes, n); }, [](const char *, size_t) {});
    if (process.get_exit_status() != 0) {
        return 0;
    }

    try {
        auto j = json::parse(out);

        // HLS: the streams have no bitrate, but their variant has one. Without
        // -map ffmpeg takes the best variant, the bitrate of the format is useless.
        int64_t sum = 0;
        int64_t variant = 0;
        for (const auto& stream : j.value("streams", json::array())) {
            std::string map = "0:" + std::to_string(number(stream, "index"));
            if (!source.maps.empty() && std::find(source.maps.begin(), source.maps.end(), map) == source.maps.end()) {
                continue;
            }

            sum += number(stream, "bit_rate");
            if (stream.contains("tags")) {
                variant = std::max(variant, number(stream["tags"], "variant_bitrate"));
            }
        }

        int64_t rate = variant > 0 ? variant : source.maps.empty() && j.contains("format") ? number(j["format"], "bit_rate") : sum;

        // less is no video
        return rate >= 10000 ? rate : 0;
    } catch (const std::exception& e) {
        ERROR("Unable to read the bitrate: {}", e.what());
        return 0;
    }
}

void run(const std::shared_ptr<Download>& d, std::vector<std::string> cmd, const DownloadSource& source, int maxRate) {
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
    while (!process.try_get_exit_status(exitStatus)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        std::lock_guard<std::mutex> lock(mutex);
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

    // left over by a crash or power loss
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        if (entry.is_regular_file() && endsWith(entry.path().filename().string(), ".mp4.part")) {
            INFO("Remove the incomplete download {}", entry.path().string());
            fs::remove(entry.path(), ec);
        }
    }
}

DownloadLookupResult lookup(const std::string& vdrChannel, const DownloadSource& source) {
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

    int duration = (int) std::lround(source.duration);
    if (duration <= 0) {
        result.error = "No video or a live stream";
        return result;
    }

    result.stream = !source.url.empty();
    result.streamName = fileName(vdrChannel + " - " + formatTime(time(nullptr), "%Y-%m-%d %H-%M"));

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

    std::set<std::string> ids = urlIds(source.url);

    struct Scored {
        double score;
        int64_t timestamp;
        DownloadCandidate candidate;
    };
    std::vector<Scored> scored;

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

        DownloadCandidate c;
        c.channel = text(e, "channel");
        c.topic = text(e, "topic");
        c.title = text(e, "title");
        c.duration = (int32_t) number(e, "duration");
        c.timestamp = number(e, "timestamp");
        c.url = url;
        c.name = candidateName(c.channel, c.topic, c.title, c.timestamp);

        double score = -5.0 * std::abs(c.duration - duration);
        if (!channel.name.empty() && lower(c.channel) == lower(channel.name)) {
            score += 20;
        } else if (channel.ard && isArd(c.channel)) {
            score += 10;
        }

        // the stream and the file of MediathekView often share an ID
        for (const auto& field : { "url_video", "url_video_hd", "url_video_low", "url_website" }) {
            std::string u = expandUrl(text(e, field), text(e, "url_video"));
            for (const auto& id : urlIds(u)) {
                if (ids.count(id) > 0) {
                    score += 100;
                    break;
                }
            }
        }

        scored.push_back({ score, c.timestamp, c });
    }

    std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
        return a.score != b.score ? a.score > b.score : a.timestamp > b.timestamp;
    });

    for (size_t i = 0; i < scored.size() && i < maxCandidates; ++i) {
        result.candidates.push_back(scored[i].candidate);
    }

    INFO("Download lookup for {} ({} s): {} entries", vdrChannel, duration, result.candidates.size());
    return result;
}

std::string start(const std::string& url, const std::string& name, int duration, int maxRate) {
    DownloadSource source;
    if (url.empty()) {
        std::lock_guard<std::mutex> lock(mutex);
        source = lastSource;
    } else {
        source.url = url;
        source.duration = duration;
    }

    if (source.url.empty()) {
        return "This video can't be downloaded";
    }

    if (source.url.rfind("http://", 0) != 0 && source.url.rfind("https://", 0) != 0) {
        return "This video can't be downloaded";
    }

    auto d = std::make_shared<Download>();

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

    cmd.insert(cmd.end(), { "-c", "copy", "-f", "mp4", "-movflags", "+faststart", "-progress", "pipe:1" });

    std::string part;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (directory.empty()) {
            return "No download directory";
        }

        d->id = std::to_string(nextId++);
        d->name = uniqueName(name);
        d->state = "running";
        d->duration = source.duration;
        part = directory + "/" + d->name + ".part";
        active[d->id] = d;
    }

    // a failure is reported at once, not later by ffmpeg in the list
    int fd = ::open(part.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (fd < 0) {
        ERROR("Unable to create {}: {}", part, strerror(errno));
        std::lock_guard<std::mutex> lock(mutex);
        active.erase(d->id);
        return "The download directory is not writable";
    }
    ::close(fd);

    {
        std::lock_guard<std::mutex> lock(mutex);
        ++running;
    }

    cmd.emplace_back(part);
    INFO("Download {} from {}", d->name, source.url);

    std::thread(run, d, cmd, source, maxRate).detach();
    return "";
}

std::vector<DownloadInfo> list() {
    std::vector<DownloadInfo> result;
    std::error_code ec;

    std::lock_guard<std::mutex> lock(mutex);
    if (directory.empty()) {
        return result;
    }

    for (const auto& kv : active) {
        const auto& d = kv.second;
        DownloadInfo info;
        info.id = d->id;
        info.name = d->name;
        info.state = d->state;
        info.progress = d->progress;
        info.error = d->error;
        auto size = fs::file_size(directory + "/" + d->name + ".part", ec);
        info.size = ec ? 0 : (int64_t) size;
        result.push_back(info);
    }

    std::vector<std::pair<fs::file_time_type, DownloadInfo>> files;
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        std::string file = entry.path().filename().string();
        if (!entry.is_regular_file(ec) || !endsWith(lower(file), ".mp4") || file[0] == '.') {
            continue;
        }

        DownloadInfo info;
        info.id = "file:" + file;
        info.name = file;
        info.state = "done";
        info.progress = 100;
        auto size = entry.file_size(ec);
        info.size = ec ? 0 : (int64_t) size;
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
        if (file.empty() || file[0] == '.' || file.find('/') != std::string::npos || !endsWith(lower(file), ".mp4")) {
            return false;
        }

        std::error_code ec;
        bool removed = fs::remove(directory + "/" + file, ec);
        INFO("Delete download {}: {}", file, removed ? "ok" : ec.message());
        return removed;
    }

    auto it = active.find(id);
    if (it == active.end()) {
        return false;
    }

    auto d = it->second;
    if (d->state == "running") {
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
        active.erase(it);
    }

    return true;
}

void shutdown() {
    std::unique_lock<std::mutex> lock(mutex);

    for (const auto& kv : active) {
        const auto& d = kv.second;
        if (d->state == "running" && !d->cancelled) {
            d->cancelled = true;
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
