#pragma once

class Linker {
public:
    enum class Type { UNKNOWN, GNU, CLANG, MSVC, IAR };

public:
    Linker(const std::string& linker);

    Type get_type() const { return m_type; }
    const std::string& get_location() const { return m_location; }

private:
    Type m_type;
    std::string m_location;
    std::vector<std::string> m_flags;
};
