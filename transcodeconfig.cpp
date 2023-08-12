#include "transcodeconfig.h"

std::vector<std::string> strsplit(std::string input, std::string delimiter) {
    size_t pos_start = 0, pos_end, delim_len = delimiter.length();
    std::string token;
    std::vector<std::string> res;

    while ((pos_end = input.find(delimiter, pos_start)) != std::string::npos) {
        token = input.substr (pos_start, pos_end - pos_start);
        pos_start = pos_end + delim_len;
        res.push_back (token);
    }

    res.push_back (input.substr (pos_start));
    return res;
}

TranscodeConfig::TranscodeConfig() {
}

bool TranscodeConfig::createConfiguration(mINI::INIStructure ini) {
    std::string audioCodecCopy = ini["audio"]["codec_copy"];

    if (!audioCodecCopy.empty()) {
        std::vector<std::string> audioCodecs = strsplit(audioCodecCopy, ",");
        allowedAudioCodecs.insert(allowedAudioCodecs.end(), audioCodecs.begin(), audioCodecs.end());
    }

    std::string audioTranscodeStr = ini["audio"]["transcode"];

    if (!audioTranscodeStr.empty() && !allowedAudioCodecs.empty()) {
        std::vector<std::string> audioParameter = strsplit(audioTranscodeStr, " ");
        audioTranscodeParameter.insert(audioTranscodeParameter.end(), audioParameter.begin(), audioParameter.end());
    }

    std::string videoCodecCopy = ini["video"]["codec_copy"];

    if (!videoCodecCopy.empty()) {
        std::vector<std::string> videoCodecs = strsplit(videoCodecCopy, ",");
        allowedVideoCodecs.insert(allowedVideoCodecs.end(), videoCodecs.begin(), videoCodecs.end());
    }

    std::string videoTranscodeStr = ini["video"]["transcode"];

    if (!videoTranscodeStr.empty() && !allowedVideoCodecs.empty()) {
        std::vector<std::string> videoParameter = strsplit(videoTranscodeStr, " ");
        videoTranscodeParameter.insert(videoTranscodeParameter.end(), videoParameter.begin(), videoParameter.end());
    }

    // some checks
    if (videoTranscodeParameter.empty() || allowedVideoCodecs.empty())  {
        videoTranscodeParameter.clear();
        allowedVideoCodecs.clear();
    }

    if (audioTranscodeParameter.empty() || allowedAudioCodecs.empty())  {
        audioTranscodeParameter.clear();
        allowedAudioCodecs.clear();
    }

    return true;
}

bool TranscodeConfig::isCopyAudio(std::string codec, std::string sample_rate) {
    if (allowedAudioCodecs.empty()) {
        return true;
    }

    return any_of(allowedAudioCodecs.begin(), allowedAudioCodecs.end(), [&](const std::string& elem) { return elem == codec; }) ||
            any_of(allowedAudioCodecs.begin(), allowedAudioCodecs.end(), [&](const std::string& elem) { return elem == codec + "." + sample_rate; });
}

bool TranscodeConfig::isCopyVideo(std::string codec) {
    if (allowedVideoCodecs.empty()) {
        return true;
    }

    return any_of(allowedVideoCodecs.begin(), allowedVideoCodecs.end(), [&](const std::string& elem) { return elem == codec; });
}

std::vector<std::string> TranscodeConfig::getAudioTranscodeParameter() {
    return audioTranscodeParameter;
}

std::vector<std::string> TranscodeConfig::getVideoTranscodeParameter() {
    return videoTranscodeParameter;
}
