#include <unistd.h>
#include <iomanip>
#include "streamplayer.h"
#include "addonhandler.h"
#include "logger.h"

extern bool verbose;
extern bool saveonly;
extern bool use_TCP;

extern std::string headers;

std::string &Trim(std::string &, const char *const);

std::string urlEncode(const std::string& str) {
    std::ostringstream encodedStream;
    encodedStream << std::hex << std::uppercase << std::setfill('0');

    for (char c : str) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encodedStream << c;
        } else {
            encodedStream << '%' << std::setw(2) << static_cast<unsigned int>(static_cast<unsigned char>(c));
        }
    }

    return encodedStream.str();
}

void usage() {
    printf("Usage: r_dash2ts -u url_to_manifest.mpd\n");
    printf("                 -a user_agent\n");
    printf("                 -c cookies\n");
    printf("                 -r referer\n");
    printf("                 -k path_to_kodi\n");
    printf("                 -p probeFfmpeg only\n");
}

int main(int argc, char *argv[]) {
    std::string url;
    std::string path_to_kodi;
    std::string user_agent;
    std::string cookies;
    std::string referer;
    int api_version = 0;
    int loglevel = 1;
    bool probeOnly = false;

    verbose = true;
    saveonly = true;
    use_TCP = false;

    int c;
    while ((c = getopt(argc, argv, "u:a:c:r:k:p")) != -1) {
        switch (c) {
            case 'u': // URL to Manifest
                url = std::string(optarg);
                continue;

            case 'a': // user agent
                user_agent = std::string(optarg);
                continue;

            case 'c': // cookies
                cookies = std::string(optarg);
                continue;

            case 'r': // referer
                referer = std::string(optarg);
                continue;

            case 'k': // path to kodi
                path_to_kodi = std::string(optarg);
                continue;

            case 'p': // probe only
                probeOnly = true;
                continue;

            default:
                usage();
                exit(EXIT_FAILURE);
        }
        break;
    }

    if (path_to_kodi.empty()) {
        // set default for VDR*ELEC
        path_to_kodi = "/storage/.kodi";
    }

    if (url.empty() || user_agent.empty() || cookies.empty() || referer.empty()) {
        usage();
        exit(EXIT_FAILURE);
    }

    logger.set_level(spdlog::level::trace);

    auto handler = new AddonHandler(path_to_kodi); // Init AddonHandler

    // Load the inputstream.adaptive Library and get the API Version
    api_version = handler->LoadAddon();

    INFO("Found inputstream.adaptive Library, version {}", api_version);

    std::string httpHeader = "Referer=" + referer + "&User-Agent=" + user_agent + "&cookie=" + urlEncode(cookies);

    // kodi 22+
    handler->AddProp("inputstream.adaptive.common_headers", httpHeader.c_str());

    // kodi 20+
    // handler->AddProp("inputstream.adaptive.stream_params", "paramname=value&paramname2=value2");
    handler->AddProp("inputstream.adaptive.manifest_headers", httpHeader.c_str());

    // kodi 21+
    handler->AddProp("inputstream.adaptive.stream_headers", httpHeader.c_str());

    // all
    handler->AddProp("inputstream.adaptive.config", "{\"internal_cookies\":true}");

    handler->SetResolution(1920, 1080);

    bool ret = handler->OpenURL(const_cast<char *>(url.c_str()));

    if (ret) {
        INFO("Open URL {}");
        if (probeOnly) {
            // print result and exit
            printf("dash2ts result:%d:%d:%d:%ld:%ld:%d,\n", handler->GetTotalTime(0, true), handler->GetTime(), handler->GetCapabilities(), handler->PositionStream(), handler->LengthStream(), handler->IsRealTimeStream() ? 1 : 0);
            exit(EXIT_SUCCESS);
        }
    } else {
        ERROR("Unable to open URL {}", url);
        exit(EXIT_FAILURE);
    }

    auto caps = handler->GetCapabilities();

    printf("Capabilities:\n");
    if (caps & INPUTSTREAM_SUPPORTS_IDEMUX) {
        printf ("  INPUTSTREAM_SUPPORTS_IDEMUX\n");
    }

    if (caps & INPUTSTREAM_SUPPORTS_IPOSTIME) {
        printf ("  INPUTSTREAM_SUPPORTS_IPOSTIME\n");
    }

    if (caps & INPUTSTREAM_SUPPORTS_IDISPLAYTIME) {
        printf ("  INPUTSTREAM_SUPPORTS_IDISPLAYTIME\n");
    }

    if (caps & INPUTSTREAM_SUPPORTS_SEEK) {
        printf ("  INPUTSTREAM_SUPPORTS_SEEK\n");
    }

    if (caps & INPUTSTREAM_SUPPORTS_PAUSE) {
        printf ("  INPUTSTREAM_SUPPORTS_PAUSE\n");
    }

    if (caps & INPUTSTREAM_SUPPORTS_ITIME) {
        printf ("  INPUTSTREAM_SUPPORTS_ITIME\n");
    }

    if (caps & INPUTSTREAM_SUPPORTS_ICHAPTER) {
        printf ("  INPUTSTREAM_SUPPORTS_ICHAPTER\n");
    }

    // 3183
    if (!handler->PosTime(3183 * 1000)) {
        printf("PosTime to 45 minutes\n");
    } else {
        printf("PosTime failed\n");
    }

    printf ("STREAM_MSEC_TO_TIME(0) = %f\n", STREAM_MSEC_TO_TIME(0));
    printf ("STREAM_MSEC_TO_TIME(45 min) = %f\n", STREAM_MSEC_TO_TIME(1000 * 60 * 45));

    printf("Time: %d\n", handler->GetTime());
    printf("TotalTime: %d\n", handler->GetTotalTime(0, true));

    // start streaming
    auto player = new StreamPlayer(0);
    player->StreamPlay(handler);

    delete handler;
    delete player;

    exit(EXIT_SUCCESS);
}
