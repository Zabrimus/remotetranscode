#pragma once

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG

#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/fmt/bin_to_hex.h"

#define TRACE(...)     SPDLOG_LOGGER_TRACE(logger.current(), __VA_ARGS__)
#define DEBUG(...)     SPDLOG_LOGGER_DEBUG(logger.current(), __VA_ARGS__)
#define INFO(...)      SPDLOG_LOGGER_INFO(logger.current(), __VA_ARGS__)
#define ERROR(...)     SPDLOG_LOGGER_ERROR(logger.current(), __VA_ARGS__)
#define CRITICAL(...)  SPDLOG_LOGGER_CRITICAL(logger.current(), __VA_ARGS__)

class Logger {

public:
    Logger();

    ~Logger();

    // Must be called before setting the desired level
    void switchToFileLogger(std::string filename);

    void set_level(spdlog::level::level_enum level) {
        _logger->set_level(level);
    }

    inline bool isTraceEnabled() {
        return _logger->level() == spdlog::level::trace;
    }

    inline bool isDebugEnabled() {
        return (_logger->level() == spdlog::level::debug) || (_logger->level() == spdlog::level::trace);
    }

    inline bool isInfoEnabled() {
        return (_logger->level() == spdlog::level::info) || (_logger->level() == spdlog::level::debug) || (_logger->level() == spdlog::level::trace);
    }

    inline spdlog::level::level_enum level() {
        return _logger->level();
    }

    inline std::shared_ptr<spdlog::logger> current() {
        return _logger;
    }

    inline bool switchedToFile() {
        return this->_switchedToFile;
    }

private:
    std::shared_ptr<spdlog::logger> _logger;
    bool _switchedToFile = false;
};

extern Logger logger;
