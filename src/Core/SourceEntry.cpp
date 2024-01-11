#include "SourceEntry.hpp"

// For print readability
extern std::filesystem::path s_project_path;
extern std::filesystem::path s_output_path;

SourceEntry::SourceEntry(const Compiler* compiler,
                         const std::filesystem::path& source_file_path,
                         const std::filesystem::path& output_directory,
                         const std::filesystem::path& object_path,
                         bool is_pch) :
    m_compiler(compiler),
    m_source_file_path(source_file_path),
    m_output_directory(output_directory),
    m_object_path(object_path),
    m_is_pch(is_pch) {
    // Log.trace(
    //     "Create {} SourceEntry {}", to_string(compiler->get_language()), std::filesystem::relative(get_source_file_path(), s_project_path));
}
