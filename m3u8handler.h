#pragma once

#include <iostream>
#include <fstream>
#include "uri_parser.h"

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"

struct media {
    std::string type;
    std::string language;
    std::string name;
    std::string groupId;
    std::string uri;
};

typedef struct {
    int width;
    int height;
    std::string url;
    std::vector<media> audio;
} m3u_stream;

class M3u8Handler {
public:
    explicit M3u8Handler(std::string uri);
    m3u_stream parseM3u();

private:
    bool startsWith(const std::string& str, const std::string& prefix);
    std::vector<std::string> split(const std::string &s, char delim);

private:
    std::string webUri;
};