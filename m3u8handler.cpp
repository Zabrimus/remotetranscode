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
            auto splitted = split(line.substr(starterStream.length()), ',');

            for (const auto& t : splitted) {
                if (startsWith(t, "RESOLUTION=")) {
                    int w, h;
                    sscanf(t.c_str() + 11, "%dx%d", &w, &h);

                    if (w > maxW) {
                        maxW = w;
                        maxH = h;
                        std::getline(stream, m3uMax);
                        if (!m3uMax.empty() && m3uMax.back() == '\r') {
                            m3uMax.pop_back();
                        }

                        m3uMax = resolveUrl(baseUri, m3uMax);
                    }
                } else if (startsWith(t, "AUDIO=")) {
                    audioGroup = t.substr(7, t.length()-8);
                }
            }
        } else if (startsWith(line, starterMedia)) {
            auto splitted = split(line.substr(starterMedia.length()), ',');

            bool addThis = true;
            media m;

            for (const auto& t : splitted) {
                if (startsWith(t, "TYPE=")) {
                    m.type = t.substr(5);
                    if (m.type != "AUDIO") {
                        // no audio -> skip
                        addThis = false;
                        break;
                    }
                } else if (startsWith(t, "LANGUAGE=")) {
                    m.language = t.substr(10, t.length()-11);
                } else if (startsWith(t, "NAME=")) {
                    m.name = t.substr(6, t.length()-7);
                } else if (startsWith(t, "GROUP-ID=")) {
                    m.groupId = t.substr(10, t.length()-11);
                } else if (startsWith(t, "URI=")) {
                    m.uri = t.substr(5, t.length()-6);
                }
            }

            if (addThis) {
                m.uri = resolveUrl(baseUri, m.uri);

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
