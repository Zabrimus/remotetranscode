#include <algorithm>
#include <climits>
#include <cstring>
#include <cstdlib>
#include "mse.h"
#include "logger.h"
#include "json.hpp"

using json = nlohmann::json;

// more data is refused, the player gets a QuotaExceededError
#define MSE_MAX_QUEUED (96 * 1024 * 1024)

// TS data sent to VDR at once
#define MSE_SEND_SIZE (64 * 1024)

// a stream without data delays the others at most this time
#define MSE_MAX_WAIT std::chrono::milliseconds(2000)

// the player may append several segments of one stream before the first of the other
#define MSE_MAX_WAIT_FIRST std::chrono::milliseconds(10000)

#define PMT_PID 0x1000
#define FIRST_PID 0x100

namespace {

constexpr uint32_t fourcc(const char *s) {
    return ((uint32_t) (uint8_t) s[0] << 24) | ((uint32_t) (uint8_t) s[1] << 16) | ((uint32_t) (uint8_t) s[2] << 8) | (uint8_t) s[3];
}

inline uint32_t rd16(const uint8_t *p) { return ((uint32_t) p[0] << 8) | p[1]; }
inline uint32_t rd32(const uint8_t *p) { return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3]; }
inline uint64_t rd64(const uint8_t *p) { return ((uint64_t) rd32(p) << 32) | rd32(p + 4); }

struct Box {
    uint32_t type;
    const uint8_t *data;    // payload
    const uint8_t *end;
    size_t size() const { return end - data; }
};

// the next complete box in [p, end)
bool nextBox(const uint8_t *&p, const uint8_t *end, Box &box) {
    if (end - p < 8) {
        return false;
    }

    uint64_t size = rd32(p);
    size_t header = 8;
    if (size == 1) {
        if (end - p < 16) {
            return false;
        }
        size = rd64(p + 8);
        header = 16;
    } else if (size == 0) {
        size = end - p;
    }

    if (size < header || size > (uint64_t) (end - p)) {
        return false;
    }

    box.type = rd32(p + 4);
    box.data = p + header;
    box.end = p + size;
    p += size;
    return true;
}

bool findBox(const uint8_t *p, const uint8_t *end, uint32_t type, Box &box) {
    while (nextBox(p, end, box)) {
        if (box.type == type) {
            return true;
        }
    }
    return false;
}

int64_t to90k(int64_t v, uint32_t timescale) {
    if (timescale == 90000 || timescale == 0) {
        return v;
    }
    return (v / timescale) * 90000 + (v % timescale) * 90000 / timescale;
}

const int aacFrequencies[] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };

class BitReader {
public:
    BitReader(const uint8_t *data, size_t size) : data(data), size(size) {}

    uint32_t read(int bits) {
        uint32_t v = 0;
        while (bits-- > 0) {
            v <<= 1;
            if (pos < size * 8) {
                v |= (data[pos / 8] >> (7 - pos % 8)) & 1;
            }
            pos++;
        }
        return v;
    }

private:
    const uint8_t *data;
    size_t size;
    size_t pos = 0;
};

// length of a MPEG-4 descriptor
size_t descriptorLength(const uint8_t *&p, const uint8_t *end) {
    size_t length = 0;
    for (int i = 0; i < 4 && p < end; ++i) {
        uint8_t b = *p++;
        length = (length << 7) | (b & 0x7F);
        if (!(b & 0x80)) {
            break;
        }
    }
    return length;
}

uint32_t crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint32_t) data[i] << 24;
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x80000000) ? (crc << 1) ^ 0x04C11DB7 : (crc << 1);
        }
    }
    return crc;
}

void putTimestamp(std::string &s, int prefix, int64_t ts) {
    uint64_t v = (uint64_t) ts & 0x1FFFFFFFFULL;
    s += (char) ((prefix << 4) | ((v >> 29) & 0x0E) | 1);
    s += (char) ((v >> 22) & 0xFF);
    s += (char) (((v >> 14) & 0xFE) | 1);
    s += (char) ((v >> 7) & 0xFF);
    s += (char) (((v << 1) & 0xFE) | 1);
}

const char startCode[] = { 0, 0, 0, 1 };

} // namespace

MseRemuxer::MseRemuxer(VdrClient *vdrClient, const std::vector<std::string> &mimeTypes) : vdrClient(vdrClient) {
    for (const auto &m : mimeTypes) {
        SourceBuffer sb;
        sb.mimeType = m;
        sourceBuffers.push_back(sb);
    }

    pesSincePat = INT_MAX;
    thread = std::thread(&MseRemuxer::run, this);
}

MseRemuxer::~MseRemuxer() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        stop = true;
    }
    condition.notify_all();

    if (thread.joinable()) {
        thread.join();
    }
}

bool MseRemuxer::append(int sourceBuffer, const std::string &data) {
    {
        std::lock_guard<std::mutex> lock(mutex);

        if (sourceBuffer < 0 || sourceBuffer >= (int) sourceBuffers.size()) {
            ERROR("MSE: unknown SourceBuffer {}", sourceBuffer);
            return false;
        }

        if (queuedBytes > MSE_MAX_QUEUED) {
            return false;
        }

        // appending reopens an ended stream
        ended = false;
        endSent = false;

        auto &sb = sourceBuffers[sourceBuffer];
        sb.pending.append(data);
        parseBoxes(sb);
    }

    condition.notify_all();
    return true;
}

void MseRemuxer::reset() {
    DEBUG("MSE: reset");

    {
        std::lock_guard<std::mutex> lock(mutex);

        generation++;

        for (auto &sb : sourceBuffers) {
            sb.pending.clear();
            for (auto &t : sb.tracks) {
                t.second.nextDts = 0;
            }
        }

        for (auto &s : streams) {
            s.queue.clear();
            s.needKey = s.video;
            s.lastDts = INT64_MIN;
        }

        queuedBytes = 0;
        out.clear();
        ended = false;
        endSent = false;
        waiting = false;
        pesSincePat = INT_MAX;
    }

    condition.notify_all();

    // after the running send, VDR starts a new stream
    std::lock_guard<std::mutex> lock(sendMutex);
    vdrClient->Seeked();
}

void MseRemuxer::endOfStream() {
    DEBUG("MSE: end of stream");

    {
        std::lock_guard<std::mutex> lock(mutex);
        ended = true;
    }
    condition.notify_all();
}

void MseRemuxer::pause() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        paused = true;
    }

    // wait for the running send, VDR shall get nothing while paused
    std::lock_guard<std::mutex> lock(sendMutex);
}

void MseRemuxer::resume() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        paused = false;
    }
    condition.notify_all();
}

std::string MseRemuxer::audioInfo() {
    std::lock_guard<std::mutex> lock(mutex);

    json result;
    result["stream"] = json::array();
    result["subtitle"] = json::array();

    int index = 0;
    for (const auto &s : streams) {
        if (s.video || s.codec == NONE) {
            continue;
        }

        json a;
        a["index"] = index++;
        a["language"] = s.language;
        a["comment"] = "";
        a["codec"] = s.codecName;
        a["channels"] = s.channels;
        a["audioDescription"] = false;
        result["stream"].push_back(a);
    }

    return result.dump();
}

// --- fragmented MP4 -----------------------------------------------------------------

void MseRemuxer::parseBoxes(SourceBuffer &sb) {
    const uint8_t *begin = (const uint8_t *) sb.pending.data();
    const uint8_t *end = begin + sb.pending.size();
    const uint8_t *p = begin;
    const uint8_t *consumed = begin;

    const uint8_t *moof = nullptr;
    size_t moofSize = 0;

    while (true) {
        const uint8_t *start = p;
        Box box;

        if (!nextBox(p, end, box)) {
            // a broken box can't be skipped
            if (end - p >= 8) {
                uint64_t size = rd32(p);
                if (size != 0 && size != 1 && size < 8) {
                    ERROR("MSE: broken box in SourceBuffer {}, drop the data", sb.mimeType);
                    consumed = end;
                }
            }
            break;
        }

        if (box.type == fourcc("moov")) {
            parseInit(sb, box.data, box.size());
            consumed = p;
        } else if (box.type == fourcc("moof")) {
            // the samples are in the following mdat
            moof = start;
            moofSize = box.end - start;
        } else if (box.type == fourcc("mdat")) {
            if (moof != nullptr) {
                parseFragment(sb, moof, moofSize, box.end);
                moof = nullptr;
            }
            consumed = p;
        } else if (moof == nullptr) {
            // ftyp, styp, sidx, emsg, ...
            consumed = p;
        }
    }

    sb.pending.erase(0, consumed - begin);

    if (sb.pending.size() > MSE_MAX_QUEUED) {
        ERROR("MSE: no complete box in SourceBuffer {}, drop the data", sb.mimeType);
        sb.pending.clear();
    }
}

int MseRemuxer::streamFor(SourceBuffer &sb, bool video) {
    // a new init segment of the SourceBuffer continues its stream
    for (const auto &t : sb.tracks) {
        if (t.second.stream >= 0 && streams[t.second.stream].video == video) {
            return t.second.stream;
        }
    }

    Stream s;
    s.pid = FIRST_PID + (int) streams.size();
    s.video = video;
    s.needKey = video;
    streams.push_back(s);
    pmtChanged = true;

    return (int) streams.size() - 1;
}

void MseRemuxer::parseInit(SourceBuffer &sb, const uint8_t *moov, size_t size) {
    const uint8_t *end = moov + size;
    std::map<uint32_t, Track> tracks;

    Box trak;
    const uint8_t *p = moov;
    while (nextBox(p, end, trak)) {
        if (trak.type != fourcc("trak")) {
            continue;
        }

        uint32_t trackId = 0;
        uint32_t timescale = 0;
        uint32_t handler = 0;
        std::string language;
        Box b, mdia, stsd;

        if (findBox(trak.data, trak.end, fourcc("tkhd"), b) && b.size() >= 24) {
            trackId = rd32(b.data + (b.data[0] == 1 ? 20 : 12));
        }

        if (!findBox(trak.data, trak.end, fourcc("mdia"), mdia)) {
            continue;
        }

        if (findBox(mdia.data, mdia.end, fourcc("mdhd"), b) && b.size() >= 24) {
            bool v1 = b.data[0] == 1;
            if (!v1 || b.size() >= 36) {
                timescale = rd32(b.data + (v1 ? 20 : 12));
                uint32_t l = rd16(b.data + (v1 ? 32 : 20));
                char lang[4] = { (char) (((l >> 10) & 0x1F) + 0x60), (char) (((l >> 5) & 0x1F) + 0x60), (char) ((l & 0x1F) + 0x60), 0 };
                if (l != 0 && std::string(lang) != "und") {
                    language = lang;
                }
            }
        }

        if (findBox(mdia.data, mdia.end, fourcc("hdlr"), b) && b.size() >= 12) {
            handler = rd32(b.data + 8);
        }

        if (handler != fourcc("vide") && handler != fourcc("soun")) {
            continue;
        }

        Box minf, stbl;
        if (!findBox(mdia.data, mdia.end, fourcc("minf"), minf) ||
            !findBox(minf.data, minf.end, fourcc("stbl"), stbl) ||
            !findBox(stbl.data, stbl.end, fourcc("stsd"), stsd) || stsd.size() < 8) {
            continue;
        }

        Box entry;
        const uint8_t *e = stsd.data + 8;
        if (!nextBox(e, stsd.end, entry)) {
            continue;
        }

        bool video = handler == fourcc("vide");
        int index = streamFor(sb, video);
        Stream &s = streams[index];

        Codec codec = NONE;
        std::string codecName;
        std::string parameterSets;
        int nalLengthSize = 4;

        if (video && entry.size() >= 78) {
            Box config;
            if ((entry.type == fourcc("avc1") || entry.type == fourcc("avc3")) &&
                findBox(entry.data + 78, entry.end, fourcc("avcC"), config) && config.size() >= 7) {
                codec = H264;
                codecName = "h264";
                nalLengthSize = (config.data[4] & 3) + 1;

                const uint8_t *q = config.data + 5;
                for (int set = 0; set < 2 && q < config.end; ++set) {
                    int count = set == 0 ? (*q++ & 0x1F) : *q++;
                    for (int i = 0; i < count && q + 2 <= config.end; ++i) {
                        size_t length = rd16(q);
                        q += 2;
                        if (q + length > config.end) {
                            break;
                        }
                        parameterSets.append(startCode, 4);
                        parameterSets.append((const char *) q, length);
                        q += length;
                    }
                }
            } else if ((entry.type == fourcc("hev1") || entry.type == fourcc("hvc1")) &&
                       findBox(entry.data + 78, entry.end, fourcc("hvcC"), config) && config.size() >= 23) {
                codec = HEVC;
                codecName = "hevc";
                nalLengthSize = (config.data[21] & 3) + 1;

                int arrays = config.data[22];
                const uint8_t *q = config.data + 23;
                for (int a = 0; a < arrays && q + 3 <= config.end; ++a) {
                    int type = q[0] & 0x3F;
                    int count = (int) rd16(q + 1);
                    q += 3;
                    for (int i = 0; i < count && q + 2 <= config.end; ++i) {
                        size_t length = rd16(q);
                        q += 2;
                        if (q + length > config.end) {
                            break;
                        }
                        if (type >= 32 && type <= 34) {
                            parameterSets.append(startCode, 4);
                            parameterSets.append((const char *) q, length);
                        }
                        q += length;
                    }
                }
            }
        } else if (!video && entry.size() >= 28) {
            int version = (int) rd16(entry.data + 8);
            int channels = (int) rd16(entry.data + 16);
            size_t children = 28 + (version == 1 ? 16 : version == 2 ? 36 : 0);

            if (entry.size() >= children) {
                if (entry.type == fourcc("mp4a")) {
                    Box esds;
                    if (findBox(entry.data + children, entry.end, fourcc("esds"), esds) && esds.size() > 4) {
                        // ES_Descriptor -> DecoderConfigDescriptor -> DecoderSpecificInfo
                        const uint8_t *q = esds.data + 4;
                        const uint8_t *asc = nullptr;
                        size_t ascLength = 0;

                        while (q + 2 <= esds.end && asc == nullptr) {
                            int tag = *q++;
                            size_t length = descriptorLength(q, esds.end);
                            const uint8_t *next = std::min(q + length, esds.end);

                            if (tag == 0x03 && q + 3 <= next) {
                                int flags = q[2];
                                q += 3;
                                if (flags & 0x80) q += 2;
                                if ((flags & 0x40) && q < next) q += 1 + *q;
                                if (flags & 0x20) q += 2;
                                continue;   // the descriptors inside
                            } else if (tag == 0x04 && q + 13 <= next) {
                                q += 13;
                                continue;
                            } else if (tag == 0x05) {
                                asc = q;
                                ascLength = next - q;
                            }

                            q = next;
                        }

                        if (asc != nullptr && ascLength >= 2) {
                            BitReader br(asc, ascLength);
                            int aot = (int) br.read(5);
                            if (aot == 31) aot = 32 + (int) br.read(6);
                            int freqIndex = (int) br.read(4);
                            int frequency = freqIndex == 15 ? (int) br.read(24) : 0;
                            int channelConfig = (int) br.read(4);

                            // HE-AAC: the core is AAC LC with the half sample rate
                            if (aot == 5 || aot == 29) {
                                int extIndex = (int) br.read(4);
                                if (extIndex == 15) br.read(24);
                                aot = (int) br.read(5);
                                if (aot == 31) aot = 32 + (int) br.read(6);
                            }

                            if (freqIndex == 15) {
                                freqIndex = 3;
                                for (int i = 0; i < 13; ++i) {
                                    if (std::abs(aacFrequencies[i] - frequency) < std::abs(aacFrequencies[freqIndex] - frequency)) {
                                        freqIndex = i;
                                    }
                                }
                            }

                            codec = AAC;
                            codecName = "aac";
                            s.aacProfile = (aot >= 1 && aot <= 4) ? aot : 2;
                            s.aacFreqIndex = std::min(freqIndex, 12);
                            s.aacChannels = channelConfig != 0 ? channelConfig : std::min(std::max(channels, 1), 7);
                        }
                    }
                } else if (entry.type == fourcc("ac-3")) {
                    codec = AC3;
                    codecName = "ac3";
                } else if (entry.type == fourcc("ec-3")) {
                    codec = EAC3;
                    codecName = "eac3";
                }
            }

            s.channels = channels;
        }

        if (codec == NONE) {
            char t[5] = { (char) (entry.type >> 24), (char) (entry.type >> 16), (char) (entry.type >> 8), (char) entry.type, 0 };
            ERROR("MSE: unsupported sample entry {} in SourceBuffer {}", t, sb.mimeType);
        }

        if (s.codec != codec || s.language != language) {
            pmtChanged = true;
        }

        s.codec = codec;
        s.codecName = codecName;
        s.timescale = timescale != 0 ? timescale : 90000;
        s.nalLengthSize = nalLengthSize;
        s.parameterSets = parameterSets;
        s.language = language;

        Track t;
        t.stream = index;
        auto old = sb.tracks.find(trackId);
        if (old != sb.tracks.end()) {
            t.nextDts = old->second.nextDts;
        }
        tracks[trackId] = t;

        DEBUG("MSE: SourceBuffer {}, track {} -> pid {}, codec {}, timescale {}", sb.mimeType, trackId, s.pid, codecName, s.timescale);
    }

    // default values of the fragments
    Box mvex, trex;
    if (findBox(moov, end, fourcc("mvex"), mvex)) {
        const uint8_t *q = mvex.data;
        while (nextBox(q, mvex.end, trex)) {
            if (trex.type != fourcc("trex") || trex.size() < 24) {
                continue;
            }

            auto t = tracks.find(rd32(trex.data + 4));
            if (t != tracks.end()) {
                t->second.defaultDuration = rd32(trex.data + 12);
                t->second.defaultSize = rd32(trex.data + 16);
                t->second.defaultFlags = rd32(trex.data + 20);
                t->second.haveDefaultFlags = true;
            }
        }
    }

    if (!tracks.empty()) {
        sb.tracks = tracks;
    }
}

void MseRemuxer::parseFragment(SourceBuffer &sb, const uint8_t *moof, size_t moofSize, const uint8_t *end) {
    const uint8_t *moofEnd = moof + moofSize;
    Box traf;
    const uint8_t *p = moof + 8;

    while (nextBox(p, moofEnd, traf)) {
        if (traf.type != fourcc("traf")) {
            continue;
        }

        Box tfhd;
        if (!findBox(traf.data, traf.end, fourcc("tfhd"), tfhd) || tfhd.size() < 8) {
            continue;
        }

        uint32_t tfhdFlags = rd32(tfhd.data) & 0xFFFFFF;
        uint32_t trackId = rd32(tfhd.data + 4);

        auto t = sb.tracks.find(trackId);
        if (t == sb.tracks.end()) {
            // another track id after a change of the representation
            if (sb.tracks.size() != 1) {
                continue;
            }
            t = sb.tracks.begin();
        }

        Track &track = t->second;
        if (track.stream < 0) {
            continue;
        }
        Stream &stream = streams[track.stream];

        uint32_t defaultDuration = track.defaultDuration;
        uint32_t defaultSize = track.defaultSize;
        uint32_t defaultFlags = track.defaultFlags;
        bool haveDefaultFlags = track.haveDefaultFlags;

        const uint8_t *q = tfhd.data + 8;
        const uint8_t *tfhdEnd = tfhd.end;
        if ((tfhdFlags & 0x01) && q + 8 <= tfhdEnd) q += 8;     // base data offset, the base is the moof
        if ((tfhdFlags & 0x02) && q + 4 <= tfhdEnd) q += 4;     // sample description index
        if ((tfhdFlags & 0x08) && q + 4 <= tfhdEnd) { defaultDuration = rd32(q); q += 4; }
        if ((tfhdFlags & 0x10) && q + 4 <= tfhdEnd) { defaultSize = rd32(q); q += 4; }
        if ((tfhdFlags & 0x20) && q + 4 <= tfhdEnd) { defaultFlags = rd32(q); haveDefaultFlags = true; }

        int64_t dts = track.nextDts;
        Box tfdt;
        if (findBox(traf.data, traf.end, fourcc("tfdt"), tfdt) && tfdt.size() >= 8) {
            dts = (tfdt.data[0] == 1 && tfdt.size() >= 12) ? (int64_t) rd64(tfdt.data + 4) : (int64_t) rd32(tfdt.data + 4);
        }

        // offset of the samples from the moof, after the header of the mdat if the trun has no offset
        const int64_t available = end - moof;
        int64_t offset = (int64_t) moofSize + 8;

        Box trun;
        const uint8_t *r = traf.data;
        bool firstTrun = true;
        while (nextBox(r, traf.end, trun)) {
            if (trun.type != fourcc("trun") || trun.size() < 8) {
                continue;
            }

            int version = trun.data[0];
            uint32_t flags = rd32(trun.data) & 0xFFFFFF;
            uint32_t count = rd32(trun.data + 4);
            const uint8_t *s = trun.data + 8;

            if (flags & 0x01) {
                if (s + 4 > trun.end) break;
                offset = (int32_t) rd32(s);
                s += 4;
            }

            uint32_t firstFlags = 0;
            bool haveFirstFlags = false;
            if (flags & 0x04) {
                if (s + 4 > trun.end) break;
                firstFlags = rd32(s);
                haveFirstFlags = true;
                s += 4;
            }

            // a fragment has some seconds, not millions of samples
            uint64_t fieldSize = 4 * (((flags & 0x100) != 0) + ((flags & 0x200) != 0) + ((flags & 0x400) != 0) + ((flags & 0x800) != 0));
            if (count > 1000000 || fieldSize * count > (uint64_t) (trun.end - s)) {
                ERROR("MSE: broken trun in SourceBuffer {}", sb.mimeType);
                break;
            }

            for (uint32_t i = 0; i < count; ++i) {
                uint32_t duration = defaultDuration;
                uint32_t size = defaultSize;
                uint32_t sampleFlags = defaultFlags;
                bool haveFlags = haveDefaultFlags;
                int64_t cto = 0;

                if (flags & 0x100) { duration = rd32(s); s += 4; }
                if (flags & 0x200) { size = rd32(s); s += 4; }
                if (flags & 0x400) { sampleFlags = rd32(s); haveFlags = true; s += 4; }
                if (flags & 0x800) { cto = version == 0 ? (int64_t) rd32(s) : (int64_t) (int32_t) rd32(s); s += 4; }

                if (i == 0 && haveFirstFlags) {
                    sampleFlags = firstFlags;
                    haveFlags = true;
                }

                if (offset < 0 || offset > available || size > (uint64_t) (available - offset)) {
                    ERROR("MSE: sample outside of the mdat in SourceBuffer {}", sb.mimeType);
                    break;
                }

                // without flags only the first sample of the fragment is known as key frame
                bool key = haveFlags ? !(sampleFlags & 0x10000) : (i == 0 && firstTrun);

                addSample(stream, dts, cto, key, moof + offset, size);

                offset += size;
                dts += duration;
            }

            firstTrun = false;
        }

        track.nextDts = dts;
    }
}

void MseRemuxer::addSample(Stream &stream, int64_t dts, int64_t cto, bool key, const uint8_t *data, size_t size) {
    if (stream.codec == NONE) {
        return;
    }

    if (stream.video) {
        if (stream.needKey && !key) {
            return;
        }
        if (key) {
            stream.needKey = false;
        }
    }

    Sample sample;
    sample.dts = to90k(dts, stream.timescale);
    sample.pts = sample.dts + to90k(cto, stream.timescale);
    sample.key = key || !stream.video;

    if (stream.codec == H264 || stream.codec == HEVC) {
        static const char audH264[] = { 0, 0, 0, 1, 0x09, (char) 0xF0 };
        static const char audHevc[] = { 0, 0, 0, 1, 0x46, 0x01, 0x50 };

        bool h264 = stream.codec == H264;
        int n = stream.nalLengthSize;

        // the parameter sets may be in the sample
        bool hasParameterSets = false;
        for (const uint8_t *q = data; q + n <= data + size;) {
            size_t length = 0;
            for (int i = 0; i < n; ++i) length = (length << 8) | q[i];
            q += n;
            if (length == 0 || length > (size_t) (data + size - q)) break;
            int type = h264 ? (q[0] & 0x1F) : ((q[0] >> 1) & 0x3F);
            if ((h264 && type == 7) || (!h264 && type >= 32 && type <= 33)) {
                hasParameterSets = true;
                break;
            }
            q += length;
        }

        sample.data.reserve(size + stream.parameterSets.size() + 64);
        if (h264) {
            sample.data.append(audH264, sizeof(audH264));
        } else {
            sample.data.append(audHevc, sizeof(audHevc));
        }

        if (key && !hasParameterSets) {
            sample.data.append(stream.parameterSets);
        }

        for (const uint8_t *q = data; q + n <= data + size;) {
            size_t length = 0;
            for (int i = 0; i < n; ++i) length = (length << 8) | q[i];
            q += n;
            if (length > (size_t) (data + size - q)) break;
            if (length > 0) {
                int type = h264 ? (q[0] & 0x1F) : ((q[0] >> 1) & 0x3F);
                // an own access unit delimiter is already there
                if (!(h264 && type == 9) && !(!h264 && type == 35)) {
                    sample.data.append(startCode, 4);
                    sample.data.append((const char *) q, length);
                }
            }
            q += length;
        }
    } else if (stream.codec == AAC) {
        // ADTS header, the frame length has 13 bits
        size_t frameLength = size + 7;
        if (frameLength > 0x1FFF) {
            ERROR("MSE: AAC frame too large ({} bytes)", size);
            return;
        }
        uint8_t adts[7];
        adts[0] = 0xFF;
        adts[1] = 0xF1;
        adts[2] = (uint8_t) (((stream.aacProfile - 1) & 3) << 6 | (stream.aacFreqIndex & 0x0F) << 2 | ((stream.aacChannels >> 2) & 1));
        adts[3] = (uint8_t) ((stream.aacChannels & 3) << 6 | ((frameLength >> 11) & 3));
        adts[4] = (uint8_t) ((frameLength >> 3) & 0xFF);
        adts[5] = (uint8_t) (((frameLength & 7) << 5) | 0x1F);
        adts[6] = 0xFC;

        sample.data.reserve(frameLength);
        sample.data.append((const char *) adts, 7);
        sample.data.append((const char *) data, size);
    } else {
        sample.data.assign((const char *) data, size);
    }

    queuedBytes += sample.data.size();
    stream.lastDts = sample.dts;
    stream.queue.push_back(std::move(sample));
}

// --- sending ------------------------------------------------------------------------------

// the stream with the next sample in decode order
bool MseRemuxer::nextSample(int &index) {
    int best = -1;
    for (int i = 0; i < (int) streams.size(); ++i) {
        if (!streams[i].queue.empty() && (best < 0 || streams[i].queue.front().dts < streams[best].queue.front().dts)) {
            best = i;
        }
    }

    if (best < 0) {
        return false;
    }

    // A stream without data may get a sample with a lower decode time.
    // Wait for it, but not forever.
    bool missing = false;
    bool first = false;
    int64_t dts = streams[best].queue.front().dts;
    for (const auto &s : streams) {
        if (s.codec != NONE && s.queue.empty() && s.lastDts < dts && !ended) {
            missing = true;
            first = first || s.lastDts == INT64_MIN;
        }
    }

    // the init segment of a SourceBuffer is still missing
    for (const auto &sb : sourceBuffers) {
        if (sb.tracks.empty() && !ended) {
            missing = true;
            first = true;
        }
    }

    if (missing) {
        auto now = std::chrono::steady_clock::now();
        if (!waiting) {
            waiting = true;
            waitStart = now;
        }
        if (now - waitStart < (first ? MSE_MAX_WAIT_FIRST : MSE_MAX_WAIT)) {
            return false;
        }
    } else {
        waiting = false;
    }

    index = best;
    return true;
}

void MseRemuxer::run() {
    std::unique_lock<std::mutex> lock(mutex);

    while (!stop) {
        if (paused) {
            condition.wait(lock);
            continue;
        }

        int index;
        if (nextSample(index)) {
            Stream &stream = streams[index];
            Sample sample = std::move(stream.queue.front());
            stream.queue.pop_front();
            queuedBytes -= sample.data.size();

            writeSample(stream, sample);

            if (out.size() < MSE_SEND_SIZE) {
                continue;
            }
        } else if (out.empty()) {
            bool allEmpty = std::all_of(streams.begin(), streams.end(), [](const Stream &s) { return s.queue.empty(); });

            if (ended && !endSent && allEmpty) {
                endSent = true;
                uint64_t gen = generation;
                lock.unlock();
                {
                    std::lock_guard<std::mutex> send(sendMutex);
                    if (gen == generation) {
                        vdrClient->EndOfStream();
                    }
                }
                lock.lock();
                continue;
            }

            condition.wait_for(lock, waiting ? std::chrono::milliseconds(100) : std::chrono::milliseconds(1000));
            continue;
        }

        // send the collected packets
        std::string data;
        data.swap(out);
        uint64_t gen = generation;
        lock.unlock();

        {
            std::lock_guard<std::mutex> send(sendMutex);

            bool current;
            {
                std::lock_guard<std::mutex> l(mutex);
                current = gen == generation && !stop;
                if (current && paused) {
                    // send it after the pause
                    out.insert(0, data);
                    current = false;
                }
            }

            if (current && !vdrClient->ProcessTSPacket(data)) {
                DEBUG("MSE: unable to send the stream to VDR");
            }
        }

        lock.lock();
    }
}

void MseRemuxer::writePacket(int pid, int &cc, bool start, const uint8_t *data, size_t length, bool withPcr, int64_t pcr) {
    uint8_t packet[188];

    size_t af = withPcr ? 8 : 0;
    if (length < 184 - af) {
        af = 184 - length;   // stuffing
    }

    packet[0] = 0x47;
    packet[1] = (uint8_t) ((start ? 0x40 : 0x00) | ((pid >> 8) & 0x1F));
    packet[2] = (uint8_t) (pid & 0xFF);
    packet[3] = (uint8_t) ((af > 0 ? 0x30 : 0x10) | (cc & 0x0F));
    cc = (cc + 1) & 0x0F;

    size_t pos = 4;
    if (af > 0) {
        packet[pos++] = (uint8_t) (af - 1);
        if (af >= 2) {
            packet[pos++] = withPcr ? 0x10 : 0x00;
            if (withPcr) {
                uint64_t base = (uint64_t) pcr & 0x1FFFFFFFFULL;
                packet[pos++] = (uint8_t) (base >> 25);
                packet[pos++] = (uint8_t) (base >> 17);
                packet[pos++] = (uint8_t) (base >> 9);
                packet[pos++] = (uint8_t) (base >> 1);
                packet[pos++] = (uint8_t) (((base & 1) << 7) | 0x7E);
                packet[pos++] = 0x00;
            }
            while (pos < 4 + af) {
                packet[pos++] = 0xFF;
            }
        }
    }

    memcpy(packet + pos, data, length);
    out.append((const char *) packet, 188);
}

void MseRemuxer::writeSection(int pid, int &cc, const std::vector<uint8_t> &section) {
    std::vector<uint8_t> payload;
    payload.push_back(0);   // pointer field
    payload.insert(payload.end(), section.begin(), section.end());

    uint32_t crc = crc32(section.data(), section.size());
    payload.push_back((uint8_t) (crc >> 24));
    payload.push_back((uint8_t) (crc >> 16));
    payload.push_back((uint8_t) (crc >> 8));
    payload.push_back((uint8_t) crc);

    writePacket(pid, cc, true, payload.data(), std::min(payload.size(), (size_t) 184));
}

void MseRemuxer::writePatPmt() {
    if (pmtChanged) {
        pmtVersion = (pmtVersion + 1) & 0x1F;
        pmtChanged = false;
    }

    // PAT: program 1 -> PMT
    std::vector<uint8_t> pat = { 0x00, 0xB0, 0x0D, 0x00, 0x01, 0xC1, 0x00, 0x00,
                                 0x00, 0x01, (uint8_t) (0xE0 | (PMT_PID >> 8)), (uint8_t) (PMT_PID & 0xFF) };
    writeSection(0, patCc, pat);

    int pcrPid = 0x1FFF;
    for (const auto &s : streams) {
        if (s.codec != NONE && (pcrPid == 0x1FFF || s.video)) {
            pcrPid = s.pid;
            if (s.video) {
                break;
            }
        }
    }

    std::vector<uint8_t> pmt = { 0x02, 0xB0, 0x00, 0x00, 0x01, (uint8_t) (0xC1 | (pmtVersion << 1)), 0x00, 0x00,
                                 (uint8_t) (0xE0 | (pcrPid >> 8)), (uint8_t) (pcrPid & 0xFF), 0xF0, 0x00 };

    for (const auto &s : streams) {
        std::vector<uint8_t> descriptors;
        int type;

        switch (s.codec) {
            case H264: type = 0x1B; break;
            case HEVC: type = 0x24; break;
            case AAC:  type = 0x0F; break;
            case AC3:  type = 0x06; descriptors = { 0x6A, 0x01, 0x00 }; break;
            case EAC3: type = 0x06; descriptors = { 0x7A, 0x01, 0x00 }; break;
            default: continue;
        }

        if (!s.video && s.language.size() == 3) {
            descriptors.insert(descriptors.end(), { 0x0A, 0x04, (uint8_t) s.language[0], (uint8_t) s.language[1], (uint8_t) s.language[2], 0x00 });
        }

        pmt.push_back((uint8_t) type);
        pmt.push_back((uint8_t) (0xE0 | (s.pid >> 8)));
        pmt.push_back((uint8_t) (s.pid & 0xFF));
        pmt.push_back((uint8_t) (0xF0 | (descriptors.size() >> 8)));
        pmt.push_back((uint8_t) (descriptors.size() & 0xFF));
        pmt.insert(pmt.end(), descriptors.begin(), descriptors.end());
    }

    size_t sectionLength = pmt.size() - 3 + 4;
    pmt[1] = (uint8_t) (0xB0 | (sectionLength >> 8));
    pmt[2] = (uint8_t) (sectionLength & 0xFF);
    writeSection(PMT_PID, pmtCc, pmt);

    pesSincePat = 0;
}

void MseRemuxer::writeSample(Stream &stream, const Sample &sample) {
    if (pmtChanged || pesSincePat >= 40 || (stream.video && sample.key)) {
        writePatPmt();
    }

    int pcrPid = -1;
    for (const auto &s : streams) {
        if (s.codec != NONE && (pcrPid < 0 || s.video)) {
            pcrPid = s.pid;
            if (s.video) {
                break;
            }
        }
    }

    int streamId;
    if (stream.video) {
        streamId = 0xE0;
    } else if (stream.codec == AAC) {
        streamId = 0xC0 + ((stream.pid - FIRST_PID) & 0x1F);
    } else {
        streamId = 0xBD;
    }

    bool hasDts = stream.video && sample.dts != sample.pts;
    int headerLength = hasDts ? 10 : 5;
    size_t pesLength = 3 + headerLength + sample.data.size();
    if (stream.video || pesLength > 0xFFFF) {
        pesLength = 0;
    }

    std::string pes;
    pes.reserve(9 + headerLength + sample.data.size());
    pes += (char) 0x00;
    pes += (char) 0x00;
    pes += (char) 0x01;
    pes += (char) streamId;
    pes += (char) ((pesLength >> 8) & 0xFF);
    pes += (char) (pesLength & 0xFF);
    pes += (char) 0x84;                         // data alignment
    pes += (char) (hasDts ? 0xC0 : 0x80);
    pes += (char) headerLength;
    putTimestamp(pes, hasDts ? 3 : 2, sample.pts);
    if (hasDts) {
        putTimestamp(pes, 1, sample.dts);
    }
    pes += sample.data;

    // PCR a bit before the decode time
    bool withPcr = stream.pid == pcrPid;
    int64_t pcr = sample.dts - 27000;

    const uint8_t *p = (const uint8_t *) pes.data();
    size_t remaining = pes.size();
    bool first = true;
    while (remaining > 0) {
        size_t room = (first && withPcr) ? 176 : 184;
        size_t n = std::min(room, remaining);
        writePacket(stream.pid, stream.cc, first, p, n, first && withPcr, pcr);
        p += n;
        remaining -= n;
        first = false;
    }

    pesSincePat++;
}
