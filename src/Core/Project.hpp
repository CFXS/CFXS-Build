#pragma once

#include <filesystem>
#include <lua.hpp>
#include "Component.hpp"

class Project {
public:
    static void initialize(const std::filesystem::path& project_path, const std::filesystem::path& output_path);
    static void uninitialize();

    static void configure();
    static void build(const std::vector<std::string>& components);
    static void clean(const std::vector<std::string>& components);

private:
    static void initialize_lua();

private: // .cfxs-build Lua bindings
    // Global
    static void lua_cfxs_print(const std::string& str);
    static bool lua_exists(const std::string& path);
    static std::string lua_get_current_directory_path(lua_State*);
    static std::string lua_get_current_script_path(lua_State*);

    static void lua_set_namespace(const std::string& ns);

    static bool lua_have_var(const std::string& var_name);

    // Import
    static void lua_import(lua_State* L);
    static void lua_import_git(lua_State* L);

    // Compiler config
    static void lua_set_c_compiler(const std::string& compiler, const std::string& standard);
    static void lua_set_cpp_compiler(const std::string& compiler, const std::string& standard);
    static void lua_set_asm_compiler(const std::string& compiler);

    static void lua_set_c_compiler_known(const std::string& version, const std::string& compiler, const std::string& standard);
    static void lua_set_cpp_compiler_known(const std::string& version, const std::string& compiler, const std::string& standard);
    static void lua_set_asm_compiler_known(const std::string& version, const std::string& compiler);

    // Linker config
    static void lua_set_linker(const std::string& linker);
    static void lua_set_archiver(const std::string& linker);

    static void lua_set_linker_known(const std::string& version, const std::string& linker);
    static void lua_set_archiver_known(const std::string& version, const std::string& linker);

    static Component& lua_create_executable(const std::string& name);
    static Component& lua_create_library(const std::string& name);

    static void lua_add_global_include_paths(lua_State* L);
    static void lua_add_global_definitions(lua_State* L);
    static void lua_add_global_compile_options(lua_State* L);
    static void lua_add_global_link_options(lua_State* L);
};
