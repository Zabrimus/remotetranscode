#include <map>
#include "m3u8handler.h"
#include "logger.h"

M3u8Handler::M3u8Handler(std::string uri, std::string userAgent, std::string referer, std::string cookies)
    : webUri(uri), userAgent(std::move(userAgent)), referer(std::move(referer)), cookies(std::move(cookies)) {
}

bool M3u8Handler::startsWith(const std::string& str, const std::string& prefix) {
    return str.size() >= prefix.size() && 0 == str.compare(0, prefix.size(), prefix);
}

std::vector<std::string> M3u8Handler::split(const std::string &s, char delim) {
    std::vector<std::string> result;
    std::stringstream ss (s);
    std::string item;

    while (getline (ss, item, delim)) {
        result.push_back (item);
    }

    return result;
}

// resolve a (relative) url of the playlist against the url of the playlist itself
static std::string resolveUrl(const std::string& base, const std::string& ref) {
    if (ref.rfind("http://", 0) == 0 || ref.rfind("https://", 0) == 0) {
        return ref;
    }

    auto schemeEnd = base.find("://");
    if (schemeEnd == std::string::npos) {
        return ref;
    }

    if (ref.rfind("//", 0) == 0) {
        // scheme relative
        return base.substr(0, schemeEnd + 1) + ref;
    }

    auto pathStart = base.find('/', schemeEnd + 3);
    std::string origin = pathStart == std::string::npos ? base : base.substr(0, pathStart);

    if (!ref.empty() && ref[0] == '/') {
        // absolute path
        return origin + ref;
    }

    // relative path: base without query and fragment, up to the last '/'
    std::string path = base.substr(0, base.find_first_of("?#"));
    auto last = path.find_last_of('/');
    if (last == std::string::npos || last < schemeEnd + 3) {
        return origin + "/" + ref;
    }

    return path.substr(0, last + 1) + ref;
}

// parse an attribute list like 'BANDWIDTH=1,CODECS="avc1.4d401f,mp4a.40.2"'.
// Commas inside quoted strings don't separate attributes, the quotes are removed.
static std::map<std::string, std::string> parseAttributes(const std::string& list) {
    std::map<std::string, std::string> result;

    size_t pos = 0;
    while (pos < list.length()) {
        auto eq = list.find('=', pos);
        if (eq == std::string::npos) {
            break;
        }

        std::string name = list.substr(pos, eq - pos);
        std::string value;

        size_t next;
        if (eq + 1 < list.length() && list[eq + 1] == '"') {
            auto close = list.find('"', eq + 2);
            if (close == std::string::npos) {
                close = list.length();
            }
            value = list.substr(eq + 2, close - eq - 2);
            next = list.find(',', close);
        } else {
            next = list.find(',', eq + 1);
            value = list.substr(eq + 1, next == std::string::npos ? std::string::npos : next - eq - 1);
        }

        // trim blanks around the name
        name.erase(0, name.find_first_not_of(' '));
        name.erase(name.find_last_not_of(' ') + 1);

        result[name] = value;

        if (next == std::string::npos) {
            break;
        }
        pos = next + 1;
    }

    return result;
}

m3u_stream M3u8Handler::parseM3u() {
    DEBUG("Load file " + webUri);

    auto uri = uri::parse_uri(webUri);

    httplib::Client cli(uri.scheme + "://" + uri.authority.host + (uri.authority.port > 0 ? ":" + std::to_string(uri.authority.port) : ""));

    // keep the query, playlists are often protected by tokens in the query
    std::string path = uri.path.empty() ? "/" : uri.path;
    if (!uri.query_string.empty()) {
        path += "?" + uri.query_string;
    }

    // same request as the browser and ffmpeg would do
    httplib::Headers headers;
    if (!userAgent.empty()) {
        headers.emplace("User-Agent", userAgent);
    }
    if (!referer.empty()) {
        headers.emplace("Referer", referer);
    }
    if (!cookies.empty()) {
        headers.emplace("Cookie", cookies);
    }

    cli.set_follow_location(true);
    auto res = cli.Get(path, headers);

    m3u_stream result;
    result.width = result.height = 0;

    if (res == nullptr) {
        return result;
    }

    if (res->status != 200) {
        return result;
    }

    // relative urls refer to the playlist after all redirects
    std::string baseUri = res->location.empty() ? webUri : res->location;

    std::string m3u = res->body;
    std::istringstream stream(m3u);
    std::string line;

    int maxW = 0, maxH = 0;
    std::string audioGroup;
    std::string m3uMax;

    std::string starterStream = "#EXT-X-STREAM-INF:";
    std::string starterMedia = "#EXT-X-MEDIA:";
    std::string starterIgnore = "#EXTINF:";

    while (std::getline(stream, line)) {
        // playlists with CRLF line endings
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (startsWith(line, starterStream)) {
            auto attr = parseAttributes(line.substr(starterStream.length()));

            if (attr.count("RESOLUTION")) {
                int w = 0, h = 0;
                sscanf(attr["RESOLUTION"].c_str(), "%dx%d", &w, &h);

                if (w > maxW) {
                    maxW = w;
                    maxH = h;

                    // the audio group of the selected variant
                    audioGroup = attr.count("AUDIO") ? attr["AUDIO"] : "";

                    std::getline(stream, m3uMax);
                    if (!m3uMax.empty() && m3uMax.back() == '\r') {
                        m3uMax.pop_back();
                    }

                    m3uMax = resolveUrl(baseUri, m3uMax);
                }
            }

        } else if (startsWith(line, starterMedia)) {
            auto attr = parseAttributes(line.substr(starterMedia.length()));

            if (attr["TYPE"] == "AUDIO") {
                media m;
                m.type = attr["TYPE"];
                m.language = attr["LANGUAGE"];
                m.name = attr["NAME"];
                m.groupId = attr["GROUP-ID"];
                m.uri = resolveUrl(baseUri, attr["URI"]);

                result.audio.push_back(m);
            }
        } else if (startsWith(line, starterIgnore)) {
            // this is already a usable m3u8. Don't process anymore but return a useful result
            result.width = 1920;
            result.height = 1080;
            result.url = webUri;
            return result;
        }
    }

    // remove all unwanted audio streams with wrong group-id
    if (!audioGroup.empty() && !result.audio.empty()) {
        auto it = result.audio.begin();
        while(it != result.audio.end()) {
            if(it->groupId != audioGroup) {
                it = result.audio.erase(it);
            } else {
                ++it;
            }
        }
    }

    result.width = maxW;
    result.height = maxH;
    result.url = m3uMax;

    return result;
}
