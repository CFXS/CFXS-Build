#include "Log.hpp"
#include <spdlog/common.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include "Core/GlobalConfig.hpp"

std::shared_ptr<spdlog::logger> e_ConsoleLogger;

void initialize_logging() {
    e_ConsoleLogger = spdlog::default_logger();

    if (GlobalConfig::log_trace())
        spdlog::set_level(spdlog::level::trace);
    else
        spdlog::set_level(spdlog::level::debug);

    e_ConsoleLogger->set_pattern("[%^%L%$] %v");
}
