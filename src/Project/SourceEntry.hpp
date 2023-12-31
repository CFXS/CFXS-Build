#pragma once
#include <filesystem>
#include "Compiler.hpp"

class SourceEntry {
public:
    SourceEntry(const Compiler* compiler, const std::filesystem::path& source_file_path, const std::filesystem::path& output_directory);

    /// Get source file path
    const std::filesystem::path& get_source_file_path() const { return m_source_file_path; }

    /// Get output object file directory
    const std::filesystem::path& get_output_directory() const { return m_output_directory; }

    const Compiler* get_compiler() const { return m_compiler; }

private:
    const Compiler* m_compiler;
    std::filesystem::path m_source_file_path; // Source file path
    std::filesystem::path m_output_directory; // Output object file directory
};