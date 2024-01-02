#pragma once

#include <filesystem>
#include <string_view>

class Compiler {
public:
    enum class Type { UNKNOWN, GNU, CLANG, MSVC, IAR };
    enum class Language { INVALID, C, CPP, ASM };
    enum class Standard { INVALID, ASM, C89, C99, C11, C17, C23, CPP98, CPP03, CPP11, CPP14, CPP17, CPP20, CPP23 };

public:
    Compiler(Language type, const std::string& location, const std::string& standard_num);
    ~Compiler();

    const Language get_language() const { return m_language; }
    const Standard get_standard() const { return m_standard; }
    const Type get_type() const { return m_type; }
    const std::string& get_location() const { return m_location; }
    const std::vector<std::string>& get_flags() const { return m_flags; }

    /// Load flags for generating dependency list
    void load_dependency_flags(std::vector<std::string>& flags, const std::filesystem::path& out_path) const;

    /// Load flags for compiling and generating object file
    void load_compile_and_output_flags(std::vector<std::string>& flags,
                                       const std::filesystem::path& source_path,
                                       const std::filesystem::path& obj_path) const;

    /// Load flags for include directories
    void push_include_directory(std::vector<std::string>& flags, const std::string& include_directory) const;

    /// Load flags for compile definitions
    void push_compile_definition(std::vector<std::string>& flags, const std::string& compile_definition) const;

    std::string_view get_object_extension() const;
    std::string_view get_dependency_extension() const;

private:
    Type m_type;
    Language m_language;
    Standard m_standard;
    std::string m_location;
    std::vector<std::string> m_flags;
};

inline std::string to_string(Compiler::Language language) {
    switch (language) {
        case Compiler::Language::C: return "C";
        case Compiler::Language::CPP: return "C++";
        case Compiler::Language::ASM: return "ASM";
        default: return "Unknown";
    }
}

inline std::string to_string(Compiler::Type type) {
    switch (type) {
        case Compiler::Type::GNU: return "GNU";
        case Compiler::Type::CLANG: return "Clang";
        case Compiler::Type::MSVC: return "MSVC";
        case Compiler::Type::IAR: return "IAR";
        default: return "Unknown";
    }
}