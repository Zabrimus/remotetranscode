#pragma once

#include <string>
#include <vector>
#include <deque>
#include <map>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <cstdint>
#include <climits>
#include "VdrClient.h"

// Media Source Extensions
//
// The browser appends the segments of the player (fragmented MP4) per SourceBuffer.
// MseRemuxer extracts the samples, interleaves the streams by their decode time and
// sends them as TS to VDR. The timestamps of the TS are the media times of the player,
// a change of the representation (new init segment) needs no restart.
class MseRemuxer {
public:
    MseRemuxer(VdrClient *vdrClient, const std::vector<std::string> &mimeTypes);
    ~MseRemuxer();

    // false: the buffer is full or the SourceBuffer is unknown
    bool append(int sourceBuffer, const std::string &data);

    // a seek: drop all data, the next segments start a new stream in VDR
    void reset();

    // send the rest and signal the end of the stream to VDR
    void endOfStream();

    // a paused stream is not sent, it continues at resume
    void pause();
    void resume();

    // audio tracks as returned by StreamHandler::getAudioInfo
    std::string audioInfo();

private:
    enum Codec { NONE, H264, HEVC, AAC, AC3, EAC3 };

    struct Sample {
        int64_t dts;            // 90 kHz
        int64_t pts;
        bool key;
        std::string data;       // the PES payload
    };

    // an elementary stream of the TS
    struct Stream {
        int pid = 0;
        bool video = false;
        Codec codec = NONE;
        uint32_t timescale = 90000;
        int nalLengthSize = 4;
        std::string parameterSets;   // SPS, PPS (VPS) with start codes
        int aacProfile = 2;
        int aacFreqIndex = 3;
        int aacChannels = 2;
        int channels = 0;
        std::string language;
        std::string codecName;

        std::deque<Sample> queue;
        bool needKey = true;    // video: drop all pictures until the first key frame
        int64_t lastDts = INT64_MIN;    // the last sample since the reset
        int cc = 0;             // continuity counter
    };

    // a track of a SourceBuffer
    struct Track {
        int stream = -1;
        uint32_t defaultDuration = 0;
        uint32_t defaultSize = 0;
        uint32_t defaultFlags = 0;
        bool haveDefaultFlags = false;
        int64_t nextDts = 0;    // decode time after the last fragment
    };

    struct SourceBuffer {
        std::string mimeType;
        std::string pending;                 // incomplete boxes of the last append
        std::map<uint32_t, Track> tracks;    // track id -> track
    };

    void parseBoxes(SourceBuffer &sb);
    void parseInit(SourceBuffer &sb, const uint8_t *moov, size_t size);
    void parseFragment(SourceBuffer &sb, const uint8_t *moof, size_t moofSize, const uint8_t *end);
    void addSample(Stream &stream, int64_t dts, int64_t cto, bool key, const uint8_t *data, size_t size);
    int streamFor(SourceBuffer &sb, bool video);

    void run();
    bool nextSample(int &stream);
    void writeSample(Stream &stream, const Sample &sample);
    void writePatPmt();
    void writePacket(int pid, int &cc, bool start, const uint8_t *data, size_t length, bool withPcr = false, int64_t pcr = 0);
    void writeSection(int pid, int &cc, const std::vector<uint8_t> &section);

    VdrClient *vdrClient;

    std::mutex mutex;
    std::condition_variable condition;
    std::mutex sendMutex;               // a running send to VDR
    std::thread thread;

    std::vector<SourceBuffer> sourceBuffers;
    std::vector<Stream> streams;
    size_t queuedBytes = 0;

    bool stop = false;
    bool paused = false;
    bool ended = false;
    bool endSent = false;
    uint64_t generation = 0;            // changes with every reset

    // TS
    std::string out;
    int patCc = 0;
    int pmtCc = 0;
    int pmtVersion = 0;
    bool pmtChanged = true;
    int pesSincePat = 0;

    std::chrono::steady_clock::time_point waitStart;
    bool waiting = false;
};
