#include "Compiler.hpp"

std::string to_string(Compiler::Type type) {
    switch (type) {
        case Compiler::Type::C: return "C";
        case Compiler::Type::CPP: return "C++";
        case Compiler::Type::ASM: return "ASM";
        default: return "Unknown";
    }
}

Compiler::Compiler(Type type, const std::string& compiler, const std::string& standard) : m_type(type) {
    Log.trace("Create {} compiler \"{}\" with standard \"{}\"", to_string(get_type()), compiler, standard);
}