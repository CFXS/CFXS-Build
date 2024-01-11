#pragma once
#include <filesystem>
#include "Compiler.hpp"

class SourceEntry {
public:
    SourceEntry(const Compiler* compiler,
                const std::filesystem::path& source_file_path,
                const std::filesystem::path& output_directory,
                const std::filesystem::path& object_path,
                bool is_pch);

    /// Get source file path
    const std::filesystem::path& get_source_file_path() const { return m_source_file_path; }

    /// Get output object file directory
    const std::filesystem::path& get_output_directory() const { return m_output_directory; }

    /// Get output object file path
    const std::filesystem::path& get_object_path() const { return m_object_path; }

    bool is_pch() const { return m_is_pch; }

    const Compiler* get_compiler() const { return m_compiler; }

private:
    const Compiler* m_compiler;
    std::filesystem::path m_source_file_path; // Source file path
    std::filesystem::path m_output_directory; // Output object file directory
    std::filesystem::path m_object_path;      // Output object file directory
    bool m_is_pch;
};

struct CompileEntry {
    const Compiler* compiler;
    std::unique_ptr<SourceEntry> source_entry;
    std::vector<std::string> compile_args;
};
