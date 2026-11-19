#pragma once

#include <string>
#include <vector>
#include "remotetranscoder_types.h"

// what is needed to download the current stream
struct DownloadSource {
    std::string url;                    // empty: no download of the stream (MSE)
    std::string cookies;
    std::string referer;
    std::string userAgent;
    std::vector<std::string> maps;      // -map options of the played streams
    double duration = 0;                // seconds, 0: unknown or live
};

// Downloads of videos as MP4 into a directory. They run in the background,
// independent of the playback.
namespace downloads {

// without a directory, downloads are disabled
void setup(const std::string& directory, const std::string& mediathekUrl);

// the entries of MediathekView with the duration of the current video
remotetranscoder::DownloadLookupResult lookup(const std::string& channel, const DownloadSource& source);

// url empty: the stream of the last lookup. maxRate in kbit/s, 0: unlimited.
// Returns an error, empty if started.
std::string start(const std::string& url, const std::string& name, int duration, int maxRate);

std::vector<remotetranscoder::DownloadInfo> list();

// cancel a running download, forget a failed one or delete a file
bool remove(const std::string& id);

// stop all running downloads
void shutdown();

}
