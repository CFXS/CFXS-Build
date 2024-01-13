#pragma once

class GlobalConfig {
public:
    // Fetch/pull git imports
    // Default = false
    // Flag: --no-git-update
    static bool skip_git_import_update();

    // How many treads to use for builds
    // Default = -1 (number of available threads)
    // Flag: -j<n>
    static int number_of_worker_threads();

    // Generate compile_commands.json
    // Default = false
    // Flag: -c
    static bool generate_compile_commands();

    // Print trace log messages
    // Default = false
    // Flag: -t
    static bool log_trace();

    // Log script printf locations
    // Default = false
    // Flag: --printf-sources
    static bool log_script_printf_locations();
};