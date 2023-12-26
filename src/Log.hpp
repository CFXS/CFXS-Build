#pragma once

#include <spdlog/spdlog.h>

void initialize_logging();

extern std::shared_ptr<spdlog::logger> e_ConsoleLogger;
extern std::shared_ptr<spdlog::logger> e_FileLogger;

#define Log     (*e_ConsoleLogger)
#define FileLog (*e_FileLogger)
