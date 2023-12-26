#include "Log.hpp"
#include <spdlog/common.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

std::shared_ptr<spdlog::logger> e_ConsoleLogger;
std::shared_ptr<spdlog::logger> e_FileLogger;

void initialize_logging() {
    e_ConsoleLogger = spdlog::default_logger();
    e_FileLogger    = spdlog::basic_logger_mt("log file", "cfxs-build-log.txt");

#ifndef PRODUCTION_BUILD
    spdlog::set_level(spdlog::level::trace);
#endif
    // e_ConsoleLogger->set_pattern("[%H:%M:%S.%e][%^%L%$] %v");
    e_ConsoleLogger->set_pattern("[%^%L%$] %v");
}
