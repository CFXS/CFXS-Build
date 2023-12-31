#include "SourceEntry.hpp"
#include <filesystem>

// For print readability
extern std::filesystem::path s_project_path;
extern std::filesystem::path s_output_path;

SourceEntry::SourceEntry(const std::filesystem::path& source_file_path, const std::filesystem::path& output_directory) :
    m_source_file_path(source_file_path), m_output_directory(output_directory) {
    Log.trace("Create SourceEntry {}", std::filesystem::relative(get_source_file_path(), s_project_path));
}
