#pragma once
#include <filesystem>
#include "SourceEntry.hpp"
#include "Compiler.hpp"
#include "Linker.hpp"

struct lua_State;

class Component {
public:
    enum class Type : int {
        EXECUTABLE,
        LIBRARY,
    };

    enum class Visibility : int {
        LOCAL,
        INHERIT,
        FORWARD,
    };

    template<typename T>
    struct ScopedValue {
        Visibility visibility;
        T value;
    };

public:
    Component(Type type,
              const std::string& name,
              const std::filesystem::path& script_path,
              const std::filesystem::path& root_path,
              const std::filesystem::path& local_output_directory);
    ~Component();

    void bind_add_sources(lua_State* L);
    void bind_add_include_paths(lua_State* L);
    void bind_add_definitions(lua_State* L);
    void bind_add_compile_options(lua_State* L);
    void bind_set_linker_script(lua_State* L);

    void configure(std::shared_ptr<Compiler> c_compiler,
                   std::shared_ptr<Compiler> cpp_compiler,
                   std::shared_ptr<Compiler> asm_compiler,
                   std::shared_ptr<Linker> linker);
    void build();
    void clean();

    Type get_type() const { return m_type; }
    const std::string& get_name() const { return m_name; }
    const std::filesystem::path& get_script_path() const { return m_script_path; }
    const std::filesystem::path& get_root_path() const { return m_root_path; }
    const std::filesystem::path& get_local_output_directory() const { return m_local_output_directory; }
    const std::filesystem::path& get_linker_script_path() const { return m_linker_script_path; }

    const std::vector<ScopedValue<std::filesystem::path>> get_include_paths() const { return m_include_paths; }
    const std::vector<ScopedValue<std::string>> get_definitions() const { return m_definitions; }
    const std::vector<ScopedValue<std::string>> get_compile_flags() const { return m_compile_flags; }

    const std::vector<std::shared_ptr<Component>>& get_dependencies() const { return m_dependencies; }
    void add_dependency(std::shared_ptr<Component> component);

    const std::vector<std::unique_ptr<CompileEntry>>& get_compile_entries() const { return m_compile_entries; }

private:
    Type m_type;
    std::string m_name;
    std::filesystem::path m_script_path;
    std::filesystem::path m_root_path;
    std::filesystem::path m_local_output_directory;

    // Component tree
    std::vector<std::shared_ptr<Component>> m_dependencies;

    // Compile
    std::vector<std::unique_ptr<CompileEntry>> m_compile_entries;

    // add_sources method
    std::vector<std::string> m_requested_sources;        // requested sources
    std::vector<std::string> m_requested_source_filters; // source filters

    std::vector<ScopedValue<std::filesystem::path>> m_include_paths;
    std::vector<ScopedValue<std::string>> m_definitions;
    std::vector<ScopedValue<std::string>> m_compile_flags;

    // linker
    std::filesystem::path m_linker_script_path;
    std::vector<std::string> m_linker_flags;
};
