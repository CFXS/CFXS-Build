#include "Core/GlobalConfig.hpp"
#include "Log.hpp"
#include <argparse/argparse.hpp>
#include <exception>
#include <filesystem>
#include "Core/Project.hpp"
#include "CommandUtils.hpp"
#include <fstream>

int get_max_ram_usage() {
#ifdef WINDOWS_BUILD
#else
    std::ifstream file("/proc/self/status");
    std::string line;

    while (std::getline(file, line)) {
        if (line.starts_with("VmPeak:")) {
            const auto pos = line.find_first_of("0123456789");
            const auto end = line.find_first_not_of("0123456789", pos);
            return std::stoi(line.substr(pos, end - pos));
        }
    }
#endif

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////
// Global Config

static bool s_config_skip_git_import_update = false;
bool GlobalConfig::skip_git_import_update() { return s_config_skip_git_import_update; }

static int s_number_of_worker_threads = 0;
int GlobalConfig::number_of_worker_threads() {
    if (s_number_of_worker_threads == 0) {
        return std::thread::hardware_concurrency();
    } else {
        return s_number_of_worker_threads;
    }
}

static bool s_generate_compile_commands = false;
bool GlobalConfig::generate_compile_commands() { return s_generate_compile_commands; }

static bool s_log_trace = false;
bool GlobalConfig::log_trace() { return s_log_trace; }

static bool s_log_script_printf_locations = false;
bool GlobalConfig::log_script_printf_locations() { return s_log_script_printf_locations; }

std::vector<std::string> e_script_definitions;

///////////////////////////////////////////////////////////////////////////////////

int main(int argc, char **argv) {
    const auto version_string = std::to_string(CFXS_BUILD_VERSION_MAJOR) + "." + std::to_string(CFXS_BUILD_VERSION_MINOR);
    argparse::ArgumentParser args("cfxs-build", version_string);

    args.add_argument("project")
        .help("Project location") //
        .required();              //

    args.add_argument("--out")
        .help("Build output directory") //
        .default_value("./")            //
        .required();                    //

    args.add_argument("--configure")
        .help("Configure project") //
        .flag();                   //

    args.add_argument("--build")
        .help("Build project")                     //
        .default_value(std::vector<std::string>()) //
        .nargs(1);

    args.add_argument("--clean")
        .help("Clean project")                     //
        .default_value(std::vector<std::string>()) //
        .nargs(1);

    args.add_argument("--skip-git-import-update") //
        .help("Skip git import update checks")    //
        .flag();

    args.add_argument("--parallel")                                                   //
        .default_value("0")                                                           //
        .implicit_value("0")                                                          //
        .help("Specify number of parallel threads to use (not specified or 0 = all)") //
        .nargs(1);                                                                    //

    args.add_argument("-c")                                                           //
        .help("Generate compile_commands.json")                                       //
        .flag();                                                                      //

    args.add_argument("-t")                                                           //
        .help("Print trace log messages")                                             //
        .flag();                                                                      //

    args.add_argument("--printf-sources")                                             //
        .help("Log script printf locations")                                          //
        .flag();                                                                      //

    args.add_argument("definitions").remaining();

    try {
        args.parse_args(argc, argv);
    } catch (const std::runtime_error &err) {
        Log.error("{}", err.what());
        std::cout << args.usage() << std::endl;
        return 1;
    }

    try {
        if (args["-t"] == true) {
            s_log_trace = true;
        }
    } catch (const std::exception &e) {
    }
    initialize_logging();

    Log.info("CFXS Build v{}", version_string);

    auto project_path = std::filesystem::path(args.get<std::string>("project"));
    auto output_path  = std::filesystem::path(args.get<std::string>("--out")) / ".cfxs/build";

    if (!project_path.is_absolute())
        project_path = std::filesystem::absolute(project_path);
    if (!output_path.is_absolute())
        output_path = std::filesystem::absolute(output_path);

    if (!std::filesystem::exists(project_path)) {
        Log.error("Project path does not exist", project_path.string());
        return 1;
    }

    const auto cfxs_build_file = project_path / ".cfxs-build";
    if (!std::filesystem::exists(cfxs_build_file)) {
        Log.error("Project does not contain a \".cfxs-build\" file", project_path.string());
        return 1;
    }

    try {
        e_script_definitions = args.get<std::vector<std::string>>("definitions");
    } catch (std::logic_error &e) {
        std::cout << "No files provided" << std::endl;
    }

    try {
        if (args["--skip-git-import-update"] == true) {
            s_config_skip_git_import_update = true;
        }

        if (args["--printf-sources"] == true) {
            s_log_script_printf_locations = true;
        }

        if (args["-c"] == true) {
            s_generate_compile_commands = true;
        }

        auto parallel_param = args.get<std::string>("--parallel");
        for (uint32_t i = 1; i <= std::thread::hardware_concurrency(); i++) {
            if (parallel_param == std::to_string(i)) {
                s_number_of_worker_threads = i;
                Log.info("Set parallel threads to {}", i);
                break;
            }
        }

        Project::initialize(project_path, output_path);

        if (args["--configure"] == true) {
            try {
                Project::configure();
            } catch (const std::runtime_error &e) {
                Log.error("Failed to configure project: {}", e.what());
                return -1;
            }
        }

        const auto build_projects = args.get<std::vector<std::string>>("--build");
        const auto clean_projects = args.get<std::vector<std::string>>("--clean");

        try {
            Project::clean(clean_projects);
        } catch (const std::runtime_error &e) {
            Log.error("Failed to clean project: {}", e.what());
            return -1;
        }

        try {
            Project::build(build_projects);
        } catch (const std::runtime_error &e) {
            Log.error("Failed to build project: {}", e.what());
            return -1;
        }
    } catch (const std::runtime_error &e) {
        return 1;
    }

    Log.trace("Exit :)");
    Log.trace(ANSI_MAGENTA "Max RAM usage: {:.1f} MB" ANSI_RESET, get_max_ram_usage() / 1024.0f / 1024.0f);

    return 0;
}
