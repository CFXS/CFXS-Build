#include "Log.hpp"
#include <argparse/argparse.hpp>
#include <filesystem>

int main(int argc, char **argv) {
    initialize_logging();

    const auto version_string = std::to_string(CFXS_BUILD_VERSION_MAJOR) + "." + std::to_string(CFXS_BUILD_VERSION_MINOR);
    argparse::ArgumentParser program("cfxs-build", version_string);

    program.add_argument("project").help("Project location").required();
    program.add_argument("--config").help("Configure project");
    program.add_argument("--build")
        .help("Build project")                    //
        .default_value("*")                       //
        .nargs(argparse::nargs_pattern::optional) //
        .flag();
    program.add_argument("--clean")
        .help("Clean project")                    //
        .default_value("*")                       //
        .nargs(argparse::nargs_pattern::optional) //
        .flag();

    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error &err) {
        Log.error("{}", err.what());
        std::cout << program.usage() << std::endl;
        return 1;
    }

    Log.info("CFXS Build v{}", version_string);

    const auto project_dir = program.get<std::string>("project");
    auto project_path      = std::filesystem::path(project_dir);

    if (!project_path.is_absolute()) {
        project_path = std::filesystem::absolute(project_path);
    }

    Log.trace("Project location: \"{}\"", project_path.string());

    if (!std::filesystem::exists(project_path)) {
        Log.error("Project path does not exist", project_path.string());
        return 1;
    }

    const auto cfxs_build_file = project_path / ".cfxs-build";
    if (!std::filesystem::exists(cfxs_build_file)) {
        Log.error("Project does not contain a \".cfxs-build\" file", project_path.string());
        return 1;
    }

    Log.trace("Exit :)");
    return 0;
}
