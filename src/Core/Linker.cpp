#include "Linker.hpp"
#include "CommandUtils.hpp"
#include "FilesystemUtils.hpp"

static std::string to_string(Linker::Type type) {
    switch (type) {
        case Linker::Type::GNU: return "GNU";
        case Linker::Type::CLANG: return "Clang";
        case Linker::Type::MSVC: return "MSVC";
        case Linker::Type::IAR: return "IAR";
        default: return "Unknown";
    }
}

Linker::~Linker() { Log.trace("Delete Linker"); }

Linker::Linker(const std::string& linker, bool known_good, const std::string& known_version) : m_location(linker) {
    Log.trace("Create linker \"{}\"", known_version, get_location());

    if (!known_good && !is_valid_program(get_location())) {
        Log.error("Linker \"{}\" not found", get_location());
        throw std::runtime_error("Linker not found");
    }

    const auto linker_version_string = known_version.empty() ? get_program_version_string(get_location()) : known_version;

    if (linker_version_string.contains("GNU") || linker_version_string.contains("gcc")) {
        m_type = Type::GNU;
    } else if (linker_version_string.contains("clang")) {
        m_type = Type::CLANG;
    } else if (linker_version_string.contains("Microsoft")) {
        m_type = Type::MSVC;
    } else if (linker_version_string.contains("IAR")) {
        m_type = Type::IAR;
    } else {
        Log.error("Linker \"{}\" is not supported", get_location());
        throw std::runtime_error("Linker not supported");
    }

    Log.trace(" - Type: {}", to_string(get_type()));
}

void Linker::load_link_flags(std::vector<std::string>& args,
                             const std::filesystem::path& output_file,
                             const std::filesystem::path& linker_script) const {
    const auto out_path         = FilesystemUtils::safe_path_string(output_file.string());
    const auto link_script_path = FilesystemUtils::safe_path_string(linker_script.string());

    switch (get_type()) {
        case Type::GNU:
            args.push_back("-o");
            args.push_back(out_path);
            if (!link_script_path.empty()) {
                args.push_back("-T");
                args.push_back(link_script_path);
            }
            break;
        case Type::CLANG:
            args.push_back("-o");
            args.push_back(out_path);
            if (!link_script_path.empty()) {
                args.push_back("-T");
                args.push_back(link_script_path);
            }
            break;
        case Type::MSVC:
            args.push_back("/OUT:");
            args.push_back(out_path);
            break;
        case Type::IAR:
            args.push_back("-o");
            args.push_back(out_path);
            if (!link_script_path.empty()) {
                args.push_back("--config");
                args.push_back(link_script_path);
            }
            break;
        default: Log.error("Linker \"{}\" is not supported", get_location()); throw std::runtime_error("Linker not supported");
    }
}

void Linker::load_input_flags(std::vector<std::string>& args, const std::filesystem::path& input_object) const {
    const auto input_path = FilesystemUtils::safe_path_string(input_object.string());

    switch (get_type()) {
        case Type::GNU: args.push_back(input_path); break;
        case Type::CLANG: args.push_back(input_path); break;
        case Type::MSVC: args.push_back(input_path); break;
        case Type::IAR: args.push_back(input_path); break;
        default: Log.error("Linker \"{}\" is not supported", get_location()); throw std::runtime_error("Linker not supported");
    }
}

void Linker::load_input_flag_extension_file(std::vector<std::string>& args, const std::filesystem::path& input_ext_file) const {
    const auto input_ext_path = FilesystemUtils::safe_path_string(input_ext_file.string());

    // commandline extension files
    switch (get_type()) {
        case Type::GNU:
        case Type::CLANG: {
            args.push_back("@" + input_ext_path);
            break;
        };
        case Type::IAR: {
            args.push_back("-f"); // command line extension without dependency
            args.push_back(input_ext_path);
            break;
        };
        default: throw std::runtime_error("Linker command line extension not supported");
    }
}

std::string_view Linker::get_executable_extension() const {
    switch (get_type()) {
        case Type::GNU: return ".elf";
        case Type::CLANG: return ".elf";
        // case Type::MSVC: return ".lib";
        case Type::IAR: return ".elf";
        default: Log.error("Linker \"{}\" is not supported", get_location()); throw std::runtime_error("Linker not supported");
    }
}
