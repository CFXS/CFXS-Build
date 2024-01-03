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