#include "Archiver.hpp"
#include <string_view>
#include "CommandUtils.hpp"

static std::string to_string(Archiver::Type type) {
    switch (type) {
        case Archiver::Type::GNU: return "GNU";
        case Archiver::Type::CLANG: return "Clang";
        case Archiver::Type::MSVC: return "MSVC";
        case Archiver::Type::IAR: return "IAR";
        default: return "Unknown";
    }
}

Archiver::~Archiver() { Log.trace("Delete Archiver"); }

Archiver::Archiver(const std::string& ar, bool known_good, const std::string& known_version) : m_location(ar) {
    Log.trace("Create archiver \"{}\"", get_location());

    if (!known_good && !is_valid_program(get_location())) {
        Log.error("Archiver \"{}\" not found", get_location());
        throw std::runtime_error("Archiver not found");
    }

    const auto ar_version_string = known_version.empty() ? get_program_version_string(get_location()) : known_version;

    if (ar_version_string.contains("GNU")) {
        m_type = Type::GNU;
    } else if (ar_version_string.contains("LLVM")) {
        m_type = Type::CLANG;
    } else if (ar_version_string.contains("Microsoft")) {
        m_type = Type::MSVC;
    } else if (ar_version_string.contains("IAR")) {
        m_type = Type::IAR;
    } else {
        Log.error("Archiver \"{}\" is not supported", get_location());
        throw std::runtime_error("Archiver not supported");
    }

    Log.trace(" - Type: {}", to_string(get_type()));
}

void Archiver::load_archive_flags(std::vector<std::string>& args, const std::filesystem::path& output_file) const {
    switch (get_type()) {
        case Type::GNU:
            args.push_back("rcs");
            args.push_back(output_file.string());
            break;
        case Type::CLANG:
            args.push_back("rcs"); // generate lib
            args.push_back(output_file.string());
            break;
        case Type::MSVC:
            args.push_back("/OUT:");
            args.push_back(output_file.string());
            break;
        case Type::IAR:
            args.push_back("-o");
            args.push_back(output_file.string());
            break;
        default: Log.error("Archiver \"{}\" is not supported", get_location()); throw std::runtime_error("Archiver not supported");
    }
}

void Archiver::load_input_flags(std::vector<std::string>& args, const std::filesystem::path& input_object) const {
    switch (get_type()) {
        case Type::GNU:
        case Type::CLANG: args.push_back(input_object.string()); break;
        case Type::MSVC: args.push_back(input_object.string()); break;
        case Type::IAR: args.push_back(input_object.string()); break;
        default: Log.error("Archiver \"{}\" is not supported", get_location()); throw std::runtime_error("Archiver not supported");
    }
}

void Archiver::load_input_flag_extension_file(std::vector<std::string>& args, const std::filesystem::path& input_ext_file) const {
    auto file_location = input_ext_file.string();

    // wrap in quotes if there is a space in the path
    file_location = file_location.find(' ') != std::string::npos ? "\"" + file_location + "\"" : file_location;

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
        default: throw std::runtime_error("Archiver command line extension not supported");
    }
}

std::string Archiver::get_archive_extension() const {
    switch (get_type()) {
        case Type::GNU: return ".a";
        case Type::CLANG: return ".a";
        case Type::MSVC: return ".lib";
        case Type::IAR: return ".a";
        default: Log.error("Archiver \"{}\" is not supported", get_location()); throw std::runtime_error("Archiver not supported");
    }
}
