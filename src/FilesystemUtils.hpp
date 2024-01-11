#pragma once
#include <filesystem>

namespace FilesystemUtils {

    template<typename... Paths>
    inline bool all_exist(Paths&&... paths) {
        return (std::filesystem::exists(paths) && ...);
    }

    inline std::string safe_path_string(const std::string& str) {
        if (str.empty())
            return str;

        if (!str.starts_with('\"') && str.contains(' ')) {
            return "\"" + str + "\"";
        }

        return str;
    }

} // namespace FilesystemUtils
