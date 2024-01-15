#pragma once
#include <filesystem>
#include "CommandUtils.hpp"

namespace FilesystemUtils {

    template<typename... Paths>
    inline bool all_exist(Paths&&... paths) {
        return (std::filesystem::exists(paths) && ...);
    }

    inline std::string safe_path_string(const std::string& str) {
        if (str.empty())
            return str;

        const auto str_mod = replace_string(str, "\\", "\\\\");

        if (!str_mod.starts_with('\"') && str_mod.contains(' ')) {
            return "\"" + str_mod + "\"";
        }

        return str_mod;
    }

} // namespace FilesystemUtils

template<typename T>
std::string path_container_to_string_with_prefix(const T& cont, const std::string& prefix) {
    std::string result;
    for (const auto& e : cont) {
        result += prefix + "\\\"" + e + "\\\" ";
    }
    if (result.length())
        result.pop_back(); // remove trailing space
    return result;
}