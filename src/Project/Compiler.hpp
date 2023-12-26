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

    const Language get_language() const { return m_language; }
    const Standard get_standard() const { return m_standard; }
    const Type get_type() const { return m_type; }
    const std::string& get_location() const { return m_location; }
    const std::vector<std::string>& get_flags() const { return m_flags; }

private:
    Type m_type;
    Language m_language;
    Standard m_standard;
    std::string m_location;
    std::vector<std::string> m_flags;
};
