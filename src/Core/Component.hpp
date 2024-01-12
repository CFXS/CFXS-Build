#pragma once
#include <filesystem>
#include <string>
#include "Core/Archiver.hpp"
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

    enum Visibility : int {
        NONE    = 0,
        PRIVATE = 1 << 0,
        PUBLIC  = 1 << 1,
    };

    struct SourceFilePath {
        SourceFilePath(const std::filesystem::path& path,
                       bool is_external,
                       const std::filesystem::path& explicit_output_directory = {},
                       bool is_precompiled_header_file                        = false) :
            path(path),
            is_external(is_external),
            explicit_output_directory(explicit_output_directory),
            is_precompiled_header_file(is_precompiled_header_file) {}

        std::filesystem::path path;
        bool is_external;
        std::filesystem::path explicit_output_directory;
        bool is_precompiled_header_file;
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
    void bind_add_library(lua_State* L);
    void bind_add_link_options(lua_State* L);
    void bind_create_precompiled_header(lua_State* L);

    void configure(std::shared_ptr<Compiler> c_compiler,
                   std::shared_ptr<Compiler> cpp_compiler,
                   std::shared_ptr<Compiler> asm_compiler,
                   std::shared_ptr<Linker> linker,
                   std::shared_ptr<Archiver> archiver);
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
    const std::vector<ScopedValue<std::string>> get_compile_options() const { return m_compile_options; }

    const std::vector<std::string> get_link_options() const { return m_link_options; }

    const std::vector<Component*>& get_libraries() const { return m_libraries; }
    void add_library(Component* component);
    const std::vector<Component*>& get_users() const { return m_used_by; }
    void add_user(Component* component);

    const std::vector<std::string>& get_precompiled_header() const { return m_precompiled_header; }

    const std::vector<std::unique_ptr<CompileEntry>>& get_compile_entries() const { return m_compile_entries; }

    Visibility get_visibility_mask_include_paths() const { return m_visibility_mask_include_paths; }
    Visibility get_visibility_mask_definitions() const { return m_visibility_mask_definitions; }
    Visibility get_visibility_mask_compile_options() const { return m_visibility_mask_compile_options; }

private:
    /// Get vector of processed source file paths
    std::vector<SourceFilePath> get_source_file_paths();

    /// Convert source path to source output directory
    std::filesystem::path get_source_output_directory(const SourceFilePath& sfp);

    /// Process source path and add to compile list if needed
    /// Return true if added to compile list
    bool process_source_file_path(const SourceFilePath& sfp,
                                  std::shared_ptr<Compiler> c_compiler,
                                  std::shared_ptr<Compiler> cpp_compiler,
                                  std::shared_ptr<Compiler> asm_compiler,
                                  bool force_compile);

private:
    static void iterate_libs(const Component* comp, std::vector<std::string>& list);

private:
    Type m_type;
    std::string m_name;
    std::filesystem::path m_script_path;
    std::filesystem::path m_root_path;
    std::filesystem::path m_local_output_directory;

    // Component tree
    std::vector<Component*> m_libraries; // Libraries that this component has added
    std::vector<Component*> m_used_by;   // Components that added this component as a library

    // Compile
    std::vector<std::unique_ptr<CompileEntry>> m_compile_entries;

    // add_sources method
    std::vector<std::string> m_requested_sources;        // requested sources
    std::vector<std::string> m_requested_source_filters; // source filters

    // Precompilled header
    std::vector<std::string> m_precompiled_header; // list of header paths to precompile

    // Definitions and options
    std::vector<ScopedValue<std::filesystem::path>> m_include_paths;
    std::vector<ScopedValue<std::string>> m_definitions;
    std::vector<ScopedValue<std::string>> m_compile_options;
    Visibility m_visibility_mask_include_paths   = Visibility::NONE;
    Visibility m_visibility_mask_definitions     = Visibility::NONE;
    Visibility m_visibility_mask_compile_options = Visibility::NONE;

    // linker
    std::shared_ptr<Archiver> m_archiver;
    std::shared_ptr<Linker> m_linker;
    std::filesystem::path m_linker_script_path;
    std::vector<std::string> m_link_options;
};

inline const char* to_string(Component::Type type) {
    switch (type) {
        case Component::Type::EXECUTABLE: return "executable";
        case Component::Type::LIBRARY: return "library";
    }
    return "???";
}

inline Component::Visibility operator|(Component::Visibility lhs, Component::Visibility rhs) {
    return (Component::Visibility)(((int)lhs) | ((int)rhs));
}
