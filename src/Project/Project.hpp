#pragma once

#include <filesystem>
#include <lua.hpp>
#include "Component.hpp"
#include "Compiler.hpp"
#include "Linker.hpp"

class Project {
public:
    static void initialize(const std::filesystem::path& project_path, const std::filesystem::path& output_path);

    static void configure();
    static void build(const std::vector<std::string>& components);
    static void clean(const std::vector<std::string>& components);

private:
    static void initialize_lua();

private: // .cfxs-build Lua bindings
    // Compiler config
    static void bind_set_c_compiler(const std::string& compiler, const std::string& standard);
    static void bind_set_cpp_compiler(const std::string& compiler, const std::string& standard);
    static void bind_set_asm_compiler(const std::string& compiler);

    // Linker config
    static void bind_set_linker(const std::string& linker);
    static void bind_set_linkerscript(const std::string& path);

    static Component& bind_create_executable(const std::string& name);
    static Component& bind_create_library(const std::string& name);
};
