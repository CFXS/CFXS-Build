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

Archiver::Archiver(const std::string& ar) : m_location(ar) {
    Log.trace("Create linker \"{}\"", get_location());

    if (!is_valid_program(get_location())) {
        Log.error("Archiver \"{}\" not found", get_location());
        throw std::runtime_error("Archiver not found");
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
        Log.error("Archiver \"{}\" is not supported", get_location());
        throw std::runtime_error("Archiver not supported");
    }

    Log.trace(" - Type: {}", to_string(get_type()));
}

void Archiver::load_archive_flags(std::vector<std::string>& args, const std::string& output_file) const {
    switch (get_type()) {
        case Type::GNU:
            args.push_back("rcs");
            args.push_back(output_file);
            break;
        case Type::CLANG:
            args.push_back("-o");
            args.push_back(output_file);
            break;
        case Type::MSVC:
            args.push_back("/OUT:");
            args.push_back(output_file);
            break;
        // case Type::IAR:
        //     args.push_back("-o");
        //     args.push_back(output_file);
        //     break;
        default: Log.error("Archiver \"{}\" is not supported", get_location()); throw std::runtime_error("Archiver not supported");
    }
}

void Archiver::load_input_flags(std::vector<std::string>& args, const std::string& input_object) const {
    switch (get_type()) {
        case Type::GNU: args.push_back(input_object); break;
        case Type::CLANG: args.push_back(input_object); break;
        case Type::MSVC: args.push_back(input_object); break;
        case Type::IAR: args.push_back(input_object); break;
        default: Log.error("Archiver \"{}\" is not supported", get_location()); throw std::runtime_error("Archiver not supported");
    }
}

std::string_view Archiver::get_archive_extension() const {
    switch (get_type()) {
        case Type::GNU: return ".a";
        case Type::CLANG: return ".a";
        case Type::MSVC: return ".lib";
        case Type::IAR: return ".a";
        default: Log.error("Archiver \"{}\" is not supported", get_location()); throw std::runtime_error("Archiver not supported");
    }
}
