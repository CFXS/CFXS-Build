#pragma once

class Archiver {
public:
    enum class Type { UNKNOWN, GNU, CLANG, MSVC, IAR };

public:
    Archiver(const std::string& ar);
    ~Archiver();

    Type get_type() const { return m_type; }
    const std::string& get_location() const { return m_location; }

    void load_archive_flags(std::vector<std::string>& args, const std::filesystem::path& output_file) const;
    void load_input_flags(std::vector<std::string>& args, const std::filesystem::path& input_object) const;
    std::string_view get_archive_extension() const;

private:
    Type m_type;
    std::string m_location;
    std::vector<std::string> m_flags;
};
