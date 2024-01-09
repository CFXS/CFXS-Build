#include "Linker.hpp"
#include "CommandUtils.hpp"

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

Linker::Linker(const std::string& linker) : m_location(linker) {
    Log.trace("Create linker \"{}\"", get_location());

    if (!is_valid_program(get_location())) {
        Log.error("Linker \"{}\" not found", get_location());
        throw std::runtime_error("Linker not found");
    }

    const auto linker_version_string = get_program_version_string(get_location());

    if (linker_version_string.contains("GNU")) {
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
    switch (get_type()) {
        case Type::GNU:
            args.push_back("-o");
            args.push_back(output_file.string());
            if (!linker_script.empty()) {
                args.push_back("-T");
                args.push_back(linker_script.string());
            }
            break;
        case Type::CLANG:
            args.push_back("-o");
            args.push_back(output_file.string());
            if (!linker_script.empty()) {
                args.push_back("-T");
                args.push_back(linker_script.string());
            }
            break;
        case Type::MSVC:
            args.push_back("/OUT:");
            args.push_back(output_file.string());
            break;
        case Type::IAR:
            args.push_back("-o");
            args.push_back(output_file.string());
            if (!linker_script.empty()) {
                args.push_back("--config");
                args.push_back(linker_script.string());
            }
            break;
        default: Log.error("Linker \"{}\" is not supported", get_location()); throw std::runtime_error("Linker not supported");
    }
}

void Linker::load_input_flags(std::vector<std::string>& args, const std::filesystem::path& input_object) const {
    switch (get_type()) {
        case Type::GNU: args.push_back(input_object.string()); break;
        case Type::CLANG: args.push_back(input_object.string()); break;
        case Type::MSVC: args.push_back(input_object.string()); break;
        case Type::IAR: args.push_back(input_object.string()); break;
        default: Log.error("Linker \"{}\" is not supported", get_location()); throw std::runtime_error("Linker not supported");
    }
}

void Linker::load_input_flag_extension_file(std::vector<std::string>& args, const std::filesystem::path& input_ext_file) const {
    // commandline extension files
    switch (get_type()) {
        case Type::GNU:
        case Type::CLANG: {
            args.push_back("@" + input_ext_file.string());
            break;
        };
        case Type::IAR: {
            args.push_back("-f"); // command line extension without dependency
            args.push_back(input_ext_file.string());
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
