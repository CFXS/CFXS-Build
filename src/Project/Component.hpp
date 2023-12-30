#pragma once
#include <filesystem>
#include "SourceEntry.hpp"
#include <lua.hpp>
#include "Compiler.hpp"

class Component {
public:
    enum class Type : int {
        EXECUTABLE,
        LIBRARY,
    };

public:
    Component(Type type,
              const std::string& name,
              const std::filesystem::path& root_path,
              const std::filesystem::path& local_output_directory);
    ~Component();

    void bind_add_sources(lua_State* L);
    void bind_add_include_directories(lua_State* L);

    void configure();
    void build(std::shared_ptr<Compiler> c_compiler, std::shared_ptr<Compiler> cpp_compiler, std::shared_ptr<Compiler> asm_compiler);
    void clean();

    Type get_type() const { return m_type; }
    const std::string& get_name() const { return m_name; }
    const std::filesystem::path& get_root_path() const { return m_root_path; }
    const std::filesystem::path& get_local_output_directory() const { return m_local_output_directory; }

private:
    Type m_type;
    std::string m_name;
    std::filesystem::path m_root_path;
    std::filesystem::path m_local_output_directory;

    // add_sources method
    std::vector<std::string> m_requested_sources;        // requested sources
    std::vector<std::string> m_requested_source_filters; // source filters

    // Actual selected source files to build
    std::vector<SourceEntry> m_source_entries;

    std::vector<std::filesystem::path> m_include_directories;
};
