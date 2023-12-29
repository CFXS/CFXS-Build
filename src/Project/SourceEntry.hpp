#pragma once
#include <filesystem>

class SourceEntry {
public:
    SourceEntry(const std::filesystem::path& path);

    const std::filesystem::path& get_path() const { return m_path; }

private:
    std::filesystem::path m_path;
};