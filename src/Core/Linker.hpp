#pragma once

class Linker {
public:
    enum class Type { UNKNOWN, GNU, CLANG, MSVC, IAR };

public:
    Linker(const std::string& linker);
    ~Linker();

    Type get_type() const { return m_type; }
    const std::string& get_location() const { return m_location; }

    void load_link_flags(std::vector<std::string>& args, const std::string& output_file) const;
    void load_input_flags(std::vector<std::string>& args, const std::string& input_object) const;
    std::string_view get_executable_extension() const;

private:
    Type m_type;
    std::string m_location;
    std::vector<std::string> m_flags;
};
