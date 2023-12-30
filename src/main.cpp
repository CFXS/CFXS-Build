#include "Log.hpp"
#include <argparse/argparse.hpp>
#include <filesystem>
#include "Project/Project.hpp"

int main(int argc, char **argv) {
    initialize_logging();

    const auto version_string = std::to_string(CFXS_BUILD_VERSION_MAJOR) + "." + std::to_string(CFXS_BUILD_VERSION_MINOR);
    argparse::ArgumentParser args("cfxs-build", version_string);

    args.add_argument("project")
        .help("Project location") //
        .required();              //

    args.add_argument("--out")
        .help("Build output directory") //
        .default_value("./.cfxs/build") //
        .required();                    //

    args.add_argument("--configure")
        .help("Configure project") //
        .flag();                   //

    args.add_argument("--build")
        .help("Build project")                          //
        .default_value(std::vector<std::string>({"*"})) //
        .nargs(argparse::nargs_pattern::any);

    args.add_argument("--clean")
        .help("Clean project")                     //
        .default_value(std::vector<std::string>()) //
        .nargs(argparse::nargs_pattern::any);

    try {
        args.parse_args(argc, argv);
    } catch (const std::runtime_error &err) {
        Log.error("{}", err.what());
        std::cout << args.usage() << std::endl;
        return 1;
    }

    Log.info("CFXS Build v{}", version_string);

    auto project_path = std::filesystem::path(args.get<std::string>("project"));
    auto output_path  = std::filesystem::path(args.get<std::string>("--out"));

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
        Project::initialize(project_path, output_path);

        if (args["--configure"] == true)
            Project::configure();

        const auto build_projects = args.get<std::vector<std::string>>("--build");
        const auto clean_projects = args.get<std::vector<std::string>>("--clean");

        Project::clean(clean_projects);
        Project::build(build_projects);
    } catch (const std::runtime_error &e) {
        return 1;
    }

    Log.trace("Exit :)");
    return 0;
}
