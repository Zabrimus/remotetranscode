#pragma once

#include "mini/ini.h"

class TranscodeConfig {
private:
    std::vector<std::string> allowedVideoCodecs;
    std::vector<std::string> allowedAudioCodecs;

    std::vector<std::string> videoTranscodeParameter;
    std::vector<std::string> audioTranscodeParameter;

public:
    explicit TranscodeConfig();
    ~TranscodeConfig() = default;

    bool createConfiguration(mINI::INIStructure ini);

    bool isCopyAudio(std::string codec, std::string sample_rate);
    bool isCopyVideo(std::string codec);

    std::vector<std::string> getAudioTranscodeParameter();
    std::vector<std::string> getVideoTranscodeParameter();
};
