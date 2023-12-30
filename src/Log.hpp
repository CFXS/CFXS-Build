#pragma once

#include <spdlog/spdlog.h>
#include <filesystem>
#include <string_view>
#include <spdlog/fmt/fmt.h>

void initialize_logging();

extern std::shared_ptr<spdlog::logger> e_ConsoleLogger;
extern std::shared_ptr<spdlog::logger> e_FileLogger;

#define Log     (*e_ConsoleLogger)
#define FileLog (*e_FileLogger)

template<>
struct fmt::formatter<std::filesystem::path> {
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) { return ctx.end(); }

    template<typename FormatContext>
    auto format(const std::filesystem::path& input, FormatContext& ctx) -> decltype(ctx.out()) {
        return fmt::format_to(ctx.out(), "{}", input.string());
    }
};

template<>
struct fmt::formatter<std::vector<std::filesystem::path>> {
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) { return ctx.end(); }

    template<typename FormatContext>
    auto format(const std::vector<std::filesystem::path>& input, FormatContext& ctx) -> decltype(ctx.out()) {
        std::string str = "";
        for (auto& s : input) {
            str += "    " + s.string() + ",\n";
        }
        str += "}";
        return fmt::format_to(ctx.out(), "std::vector<std::filesystem::path> ({}) {{{}{}", input.size(), input.size() ? "\n" : "", str);
    }
};

template<>
struct fmt::formatter<std::vector<std::string>> {
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) { return ctx.end(); }

    template<typename FormatContext>
    auto format(const std::vector<std::string>& input, FormatContext& ctx) -> decltype(ctx.out()) {
        std::string str = "";
        for (auto& s : input) {
            str += "    \"" + s + "\",\n";
        }
        str += "}";
        return fmt::format_to(ctx.out(), "std::vector<std::string> ({}) {{{}{}", input.size(), input.size() ? "\n" : "", str);
    }
};