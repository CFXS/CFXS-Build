#pragma once

class Linker {
public:
    enum class Type { UNKNOWN, GNU, CLANG, MSVC, IAR };

public:
    Linker(const std::string& linker, bool known_good = false, const std::string& known_version = {});
    ~Linker();

    Type get_type() const { return m_type; }
    const std::string& get_location() const { return m_location; }

    void load_link_flags(std::vector<std::string>& args,
                         const std::filesystem::path& output_file,
                         const std::filesystem::path& linker_script = {}) const;
    void load_input_flags(std::vector<std::string>& args, const std::filesystem::path& input_object) const;
    void load_input_flag_extension_file(std::vector<std::string>& args, const std::filesystem::path& input_ext_file) const;

    std::string_view get_executable_extension() const;

private:
    Type m_type;
    std::string m_location;
    std::vector<std::string> m_flags;
};
