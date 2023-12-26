#include <filesystem>
#include <string_view>

class Compiler {
public:
    enum class Type { C, CPP, ASM };
    enum class Standard { ASM, C89, C99, C11, C17, C23, CPP98, CPP03, CPP11, CPP14, CPP17, CPP20 };

public:
    Compiler(Type type, const std::string& compiler, const std::string& standard);

    const Type get_type() const { return m_type; }
    const std::string& get_location() const { return m_location; }
    const std::vector<std::string>& get_flags() const { return m_flags; }

private:
    Type m_type;
    Standard m_standard;
    std::string m_location;
    std::vector<std::string> m_flags;
};
