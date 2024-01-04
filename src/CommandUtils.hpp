#pragma once

#ifdef WINDOWS_BUILD
#define _HAS_CXX17 1
#endif
#include <filesystem>
#include <subprocess.h>

#define ANSI_RESET      "\033[0m"
#define ANSI_GREEN      "\033[92m"
#define ANSI_RED        "\033[91m"
#define ANSI_GRAY       "\033[90m"
#define ANSI_YELLOW     "\033[33m"
#define ANSI_MAGENTA    "\033[35m"
#define ANSI_CYAN       "\033[36m"
#define ANSI_ORANGE     "\033[38;5;208m"
#define ANSI_LIGHT_GRAY "\033[38;5;250m"

inline bool is_valid_file_path(const std::string& str) {
    return std::filesystem::exists(str); //
}

inline bool is_valid_program(const std::string& str) {
#if WINDOWS_BUILD == 1
    auto s = "where " + str + " > nul 2>&1";
    return system(s.c_str()) == 0;
#else
    auto s = "type " + str + " > /dev/null";
    return system(s.c_str()) == 0;
#endif
}

inline std::string get_program_version_string(const std::string& location) {
    const char* command_line[] = {location.c_str(), "--version", NULL};
    struct subprocess_s process;
    int res = subprocess_create(command_line,
                                subprocess_option_combined_stdout_stderr | //
                                    subprocess_option_enable_async |       //
                                    subprocess_option_search_user_path,    //
                                &process);
    if (res != 0) {
        Log.error("[create {}] Failed to get program version string of \"{}\"", res, location);
        throw std::runtime_error("Failed to get program version string");
    }

    int process_ret = -1;
    res             = subprocess_join(&process, &process_ret);
    if (res != 0) {
        Log.error("[join {}] Failed to get program version string of \"{}\"", res, location);
        throw std::runtime_error("Failed to get program version string");
    }

    if (process_ret < 0) {
        Log.error("[execute {}] Failed to get program version string of \"{}\"", process_ret, location);
        throw std::runtime_error("Failed to get program version string");
    }

    FILE* p_stdout = subprocess_stdout(&process);

    // read all contents of p_stdout to std::string
    std::string result;
    char buf[256];
    while (fgets(buf, sizeof(buf), p_stdout) != NULL) {
        result += buf;
    }

    return result;
}

inline std::pair<int, std::string> execute_with_args(const std::string& cmd, const std::vector<std::string>& args) {
    std::vector<const char*> command_line = {cmd.c_str()};
    for (const auto& a : args) {
        command_line.push_back(a.c_str());
    }
    command_line.push_back(NULL);

    // Log.debug("[Execute] {} {}", cmd, args);

    struct subprocess_s process;
    int res = subprocess_create(command_line.data(),
                                subprocess_option_combined_stdout_stderr |                                      //
                                    subprocess_option_enable_async |                                            //
                                    subprocess_option_search_user_path | subprocess_option_inherit_environment, //
                                &process);
    if (res != 0) {
        Log.error("[create {}] Failed to execute with args", res);
        throw std::runtime_error("Failed to execute");
    }

    int process_ret = -1;
    res             = subprocess_join(&process, &process_ret);
    if (res != 0) {
        Log.error("[join {}] Failed to execute with args", res);
        throw std::runtime_error("Failed to execute");
    }

    FILE* p_stdout = subprocess_stdout(&process);

    // read all contents of p_stdout to std::string
    std::string result;
    char buf[256];
    while (fgets(buf, sizeof(buf), p_stdout) != NULL) {
        result += buf;
    }

    return {process_ret, result};
}

template<typename T>
concept ContainerObject = requires(T x) {
    x.begin();
    x.end();
};

template<ContainerObject T, typename U>
auto container_count(const T& cont, const U& val) {
    return std::count(cont.begin(), cont.end(), val);
}
