#include "SourceEntry.hpp"
#include <filesystem>

// For print readability
extern std::filesystem::path s_project_path;
extern std::filesystem::path s_output_path;

SourceEntry::SourceEntry(const std::filesystem::path& source_file_path, const std::filesystem::path& output_directory) :
    m_source_file_path(source_file_path), m_output_directory(output_directory) {
    Log.trace("Create SourceEntry for {} - output to {}",
              std::filesystem::relative(get_source_file_path(), s_project_path),
              std::filesystem::relative(get_output_directory(), s_project_path));

    // Generate output_directory if it does not exist
    if (!std::filesystem::exists(get_output_directory())) {
        try {
            std::filesystem::create_directories(get_output_directory());
        } catch (const std::exception& e) {
            Log.error("Failed to create output dir [{}]: {}", get_output_directory(), e.what());
        }
    }
}