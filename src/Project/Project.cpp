#include "Project.hpp"
#include <filesystem>
#include <fstream>
#include <functional>
#include <LuaBridge/LuaBridge.h>
#include "Project/Component.hpp"
#include "lauxlib.h"
#include <lua.hpp>
#include <re2/re2.h>
#include <regex>

lua_State* s_MainLuaState;
std::vector<std::shared_ptr<Component>> s_components;

std::filesystem::path s_project_path;
std::filesystem::path s_output_path;

// Project state
std::shared_ptr<Compiler> s_c_compiler;
std::shared_ptr<Compiler> s_cpp_compiler;
std::shared_ptr<Compiler> s_asm_compiler;
std::shared_ptr<Linker> s_linker;

std::vector<std::string> s_global_c_compile_flags;
std::vector<std::string> s_global_cpp_compile_flags;
std::vector<std::string> s_global_asm_compile_flags;
std::vector<std::string> s_global_link_flags;

std::filesystem::file_time_type get_last_modified_time(const std::filesystem::path& path) { return std::filesystem::last_write_time(path); }

#define LuaLog(...)   Log.info("[\033[1;36mLua\033[0m] " __VA_ARGS__)
#define LuaError(...) Log.error("[\033[1;36mLua\033[0m] " __VA_ARGS__)
#define LuaWarn(...)  Log.warn("[\033[1;36mLua\033[0m] " __VA_ARGS__)

std::string read_source(const std::filesystem::path& path) {
    std::ifstream fs(path.string());
    std::stringstream buffer;
    buffer << fs.rdbuf();
    return buffer.str();
}

void Project::initialize(const std::filesystem::path& project_path, const std::filesystem::path& output_path) {
    s_project_path = project_path;
    s_output_path  = output_path;

    Log.trace("Project location: \"{}\"", project_path.string());
    Log.trace("Output location: \"{}\"", output_path.string());

    // create output_path directory if it does not exist and throw if failed to create
    if (!std::filesystem::exists(output_path)) {
        if (!std::filesystem::create_directories(output_path)) {
            Log.error("Failed to create output directory", output_path.string());
            throw std::runtime_error("Failed to create output directory");
        }
    }

    initialize_lua();
}
void Project::configure() {
    Log.info("Configure project");

    const auto source_location = s_project_path / ".cfxs-build";
    const auto root_buildfile  = read_source(source_location);
    // execute root_buildfile into lua state
    if (luaL_dostring(s_MainLuaState, root_buildfile.c_str())) {
        // get and log lua error callstack
        std::string error = lua_tostring(s_MainLuaState, -1);

        std::regex error_regex(R"(\[string\s\"(.*)\"\]:(\d+):)");
        std::smatch match;
        if (std::regex_search(error, match, error_regex)) {
            if (match.size() == 3) {
                error = fmt::format("\"{}:{}\"{}", source_location.string(), match[2].str(), error.substr(match[0].str().length()));
            }
        }

        LuaError("{}", error);
        throw std::runtime_error("Failed to configure");
    } else {
        auto comp = s_components[0];
        comp->configure();
    }
}

void Project::build(const std::vector<std::string>& components) {
    if (std::find(components.begin(), components.end(), "*") != components.end()) {
        for (auto& comp : s_components) {
            comp->build(s_c_compiler, s_cpp_compiler, s_asm_compiler);
        }
    } else {
        for (auto& c : components) {
            auto comp = std::find_if(s_components.begin(), s_components.end(), [&c](const auto& comp) {
                return comp->get_name() == c;
            });
            if (comp != s_components.end()) {
                (*comp)->build(s_c_compiler, s_cpp_compiler, s_asm_compiler);
            } else {
                Log.error("Component \"{}\" does not exist", c);
                throw std::runtime_error("Component does not exist");
            }
        }
    }
}

void Project::clean(const std::vector<std::string>& components) {
    if (std::find(components.begin(), components.end(), "*") != components.end()) {
        for (auto& comp : s_components) {
            comp->clean();
        }
    } else {
        for (auto& c : components) {
            auto comp = std::find_if(s_components.begin(), s_components.end(), [&c](const auto& comp) {
                return comp->get_name() == c;
            });
            if (comp != s_components.end()) {
                (*comp)->clean();
            } else {
                Log.error("Component \"{}\" does not exist", c);
                throw std::runtime_error("Component does not exist");
            }
        }
    }
}

///////////////////////////////////////////////////////
// [Lua Bindings]

#define TO_FUNCTION(f) std::function<decltype(f)>(f)

void Project::initialize_lua() {
    Log.trace("Initialize Lua");

    auto& L = s_MainLuaState;

    // initialize Lua state
    L = luaL_newstate();
    if (!L) {
        Log.error("Failed to create Lua state");
        throw std::runtime_error("Failed to create Lua state");
    }

    // load Lua libraries
    luaL_openlibs(L);

// Set _G.OS to Windows or Unix
#if WINDOWS_BUILD == 1
    luaL_loadstring(L, "_G.OS = \"Windows\"");
#else
    luaL_loadstring(L, "_G.OS = \"Unix\"");
#endif

    auto bridge = luabridge::getGlobalNamespace(L);

    bridge.addFunction<void, const std::string&, const std::string&>("set_c_compiler", TO_FUNCTION(bind_set_c_compiler));
    bridge.addFunction<void, const std::string&, const std::string&>("set_cpp_compiler", TO_FUNCTION(bind_set_cpp_compiler));
    bridge.addFunction<void, const std::string&>("set_asm_compiler", TO_FUNCTION(bind_set_asm_compiler));

    bridge.addFunction<void, const std::string&>("set_linker", TO_FUNCTION(bind_set_linker));
    bridge.addFunction<void, const std::string&>("set_linkerscript", TO_FUNCTION(bind_set_linkerscript));

    bridge.addFunction<Component&, const std::string&>("create_executable", TO_FUNCTION(bind_create_executable));
    bridge.addFunction<Component&, const std::string&>("create_library", TO_FUNCTION(bind_create_library));

    bridge.beginClass<Component>("$Component")
        .addFunction("add_sources", &Component::bind_add_sources)
        .addFunction("add_include_directories", &Component::bind_add_include_directories)
        .addFunction("add_compile_definitions", &Component::bind_add_compile_definitions)
        .addFunction("add_compile_options", &Component::bind_add_compile_options)
        .endClass();
}

// Compiler config
void Project::bind_set_c_compiler(const std::string& compiler, const std::string& standard) {
    s_c_compiler = std::make_shared<Compiler>(Compiler::Language::C, compiler, standard); //
}

void Project::bind_set_cpp_compiler(const std::string& compiler, const std::string& standard) {
    s_cpp_compiler = std::make_shared<Compiler>(Compiler::Language::CPP, compiler, standard); //
}

void Project::bind_set_asm_compiler(const std::string& compiler) {
    s_asm_compiler = std::make_shared<Compiler>(Compiler::Language::ASM, compiler, "ASM"); //
}

// Linker config
void Project::bind_set_linker(const std::string& linker) {
    s_linker = std::make_shared<Linker>(linker); //
}

void Project::bind_set_linkerscript(const std::string& path) {}

// Component creation
Component& Project::bind_create_executable(const std::string& name) {
    // valid filename match
    if (!RE2::FullMatch(name, R"(^[a-zA-Z0-9_\- ]+$)")) {
        luaL_error(s_MainLuaState,
                   "Invalid executable name [%s] - name can only contain alphanumeric characters, dashes and underscores",
                   name.c_str());
        throw std::runtime_error("Invalid executable name");
    }

    auto comp = std::make_shared<Component>(Component::Type::EXECUTABLE, name, s_project_path, s_output_path / "cb" / name);
    s_components.push_back(comp);
    return *comp.get();
}

Component& Project::bind_create_library(const std::string& name) {
    // valid filename match
    if (!RE2::FullMatch(name, R"(^[a-zA-Z0-9_\- ]+$)")) {
        luaL_error(s_MainLuaState,
                   "Invalid library name [%s] - name can only contain alphanumeric characters, dashes and underscores",
                   name.c_str());
        throw std::runtime_error("Invalid library name");
    }

    auto comp = std::make_shared<Component>(Component::Type::LIBRARY, name, s_project_path, s_output_path / "cb" / name);
    s_components.push_back(comp);
    return *comp.get();
}