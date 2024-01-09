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
    static void bind_cfxs_print(const std::string& str);

    // Import
    static void bind_import(lua_State* L);
    static void bind_import_git(lua_State* L);

    // Compiler config
    static void bind_set_c_compiler(const std::string& compiler, const std::string& standard);
    static void bind_set_cpp_compiler(const std::string& compiler, const std::string& standard);
    static void bind_set_asm_compiler(const std::string& compiler);

    // Linker config
    static void bind_set_linker(const std::string& linker);
    static void bind_set_archiver(const std::string& linker);

    static Component& bind_create_executable(const std::string& name);
    static Component& bind_create_library(const std::string& name);

    static void bind_add_global_include_paths(lua_State* L);
    static void bind_add_global_definitions(lua_State* L);
    static void bind_add_global_compile_options(lua_State* L);
    static void bind_add_global_link_options(lua_State* L);
};
