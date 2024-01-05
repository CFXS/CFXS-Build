#include "Project.hpp"
#include <filesystem>
#include <fstream>
#include <functional>
#include <LuaBridge/LuaBridge.h>
#include "Core/Component.hpp"
#include "Core/GIT.hpp"
#include "lauxlib.h"
#include <lua.hpp>
#include "LuaBackend.hpp"
#include "RegexUtils.hpp"
#include "lua.h"
#include <regex>
#include <CommandUtils.hpp>
#include <stdexcept>

// folder inside of build path to write build files to
#define BUILD_TEMP_LOCATION    "components"
#define EXTERNAL_TEMP_LOCATION "external"

lua_State* s_MainLuaState;
std::vector<std::shared_ptr<Component>> s_components;

std::filesystem::path s_project_path;
std::filesystem::path s_output_path;
std::vector<std::filesystem::path> s_script_path_stack;
std::vector<std::filesystem::path> s_source_location_stack;

// Project state
std::shared_ptr<Compiler> s_c_compiler;
std::shared_ptr<Compiler> s_cpp_compiler;
std::shared_ptr<Compiler> s_asm_compiler;
std::shared_ptr<Linker> s_linker;

std::vector<std::string> s_global_c_compile_flags;
std::vector<std::string> s_global_cpp_compile_flags;
std::vector<std::string> s_global_definitions;
std::vector<std::filesystem::path> s_global_include_paths;
std::vector<std::string> s_global_asm_compile_flags;
std::vector<std::string> s_global_link_flags;

std::filesystem::file_time_type get_last_modified_time(const std::filesystem::path& path) { return std::filesystem::last_write_time(path); }

std::string read_source(const std::filesystem::path& path) {
    std::ifstream fs(path.string());
    std::stringstream buffer;
    buffer << fs.rdbuf();
    return buffer.str();
}

void Project::uninitialize() {
    s_c_compiler.reset();
    s_cpp_compiler.reset();
    s_asm_compiler.reset();
    s_linker.reset();
    s_components.clear();
}

void Project::initialize(const std::filesystem::path& project_path, const std::filesystem::path& output_path) {
    s_project_path = std::filesystem::weakly_canonical(project_path);
    s_output_path  = std::filesystem::weakly_canonical(output_path);

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

static void print_traceback(const std::filesystem::path& source_location) {
    std::string error = lua_tostring(s_MainLuaState, -1);

    std::regex error_regex(R"((.*)\:(\d+):)");
    std::smatch match;
    std::string source = "";
    if (std::regex_search(error, match, error_regex)) {
        if (match.size() == 3) {
            source = fmt::format("{}:{}", source_location.string(), match[2].str());
            error  = error.substr(match[0].str().length() + 1);
        }
    }

    luaL_traceback(s_MainLuaState, s_MainLuaState, nullptr, 1);
    std::string traceback = lua_tostring(s_MainLuaState, -1);

    if (traceback != "stack traceback:") {
        traceback = traceback.substr(strlen("stack traceback:") + 1);
        LuaError("{}\n{}Call Trace:\n\t{}{}{}\n{}{}\n", error, ANSI_RED, ANSI_RESET, source, ANSI_GRAY, traceback, ANSI_RESET);
    } else {
        LuaError("{}\n{}Call Trace:\n\t{}{}\n", error, ANSI_RED, ANSI_RESET, source, ANSI_RESET);
    }
}

void Project::configure() {
    Log.info("Configure Project");
    const auto t1 = std::chrono::high_resolution_clock::now();

    const auto source_location = s_project_path / ".cfxs-build";
    const auto root_buildfile  = read_source(source_location);

    // root script path
    s_script_path_stack     = {s_project_path};
    s_source_location_stack = {source_location};

    try {
        // execute root_buildfile into lua state
        if (luaL_dofile(s_MainLuaState, source_location.string().c_str())) {
            // get and log lua error callstack
            print_traceback(source_location);
            throw std::runtime_error("Failed to execute script");
        } else {
            for (auto& comp : s_components) {
                comp->configure(s_c_compiler, s_cpp_compiler, s_asm_compiler, s_linker);
            }
        }
    } catch (const std::runtime_error& e) {
        throw e;
    }

    const auto t2 = std::chrono::high_resolution_clock::now();
    auto ms       = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    Log.info("Project configure done in {:.3f}s", ms / 1000.0f);
}

void Project::build(const std::vector<std::string>& components) {
    Log.info("Build Project");
    const auto t1 = std::chrono::high_resolution_clock::now();

    if (std::find(components.begin(), components.end(), "*") != components.end()) {
        for (auto& comp : s_components) {
            comp->build();
        }
    } else {
        for (auto& c : components) {
            auto comp = std::find_if(s_components.begin(), s_components.end(), [&c](const auto& comp) {
                return comp->get_name() == c;
            });
            if (comp != s_components.end()) {
                (*comp)->build();
            } else {
                Log.error("Component \"{}\" does not exist", c);
                throw std::runtime_error("Component does not exist");
            }
        }
    }

    const auto t2 = std::chrono::high_resolution_clock::now();
    auto ms       = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    Log.info("Project build done in {:.3f}s", ms / 1000.0f);
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
    luaL_loadstring(L, "_G.Platform = \"Windows\"");
#else
    luaL_loadstring(L, "_G.Platform = \"Unix\"");
#endif

    // remove some library functions
    static constexpr const char* REMOVE_GLOBALS[] = {
        "load",
        "warn",
        "coroutine",
        "loadfile",
        "dofile",
        "io",
        "package",
        "require",
    };
    for (auto name : REMOVE_GLOBALS) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }

    // remove some library os functions
    static constexpr const char* REMOVE_OS_FUNCTIONS[] = {
        "remove",
        "execute",
        "rename",
        "setlocale",
        "exit",
    };

    for (auto name : REMOVE_OS_FUNCTIONS) {
        lua_getglobal(L, "os");
        lua_pushstring(L, name);
        lua_pushnil(L);
        lua_settable(L, -3);
        lua_pop(L, 1);
    }

    auto bridge = luabridge::getGlobalNamespace(L);

    bridge.addFunction<void, const std::string&, const std::string&>("set_c_compiler", TO_FUNCTION(bind_set_c_compiler));
    bridge.addFunction<void, const std::string&, const std::string&>("set_cpp_compiler", TO_FUNCTION(bind_set_cpp_compiler));
    bridge.addFunction<void, const std::string&>("set_asm_compiler", TO_FUNCTION(bind_set_asm_compiler));

    bridge.addFunction<void, const std::string&>("set_linker", TO_FUNCTION(bind_set_linker));

    bridge.addFunction<void, lua_State*>("import", TO_FUNCTION(bind_import));
    bridge.addFunction<void, lua_State*>("import_git", TO_FUNCTION(bind_import_git));

    bridge.addFunction<void, lua_State*>("add_global_include_paths", TO_FUNCTION(bind_add_global_include_paths));
    bridge.addFunction<void, lua_State*>("add_global_definitions", TO_FUNCTION(bind_add_global_definitions));
    bridge.addFunction<void, lua_State*>("add_global_compile_options", TO_FUNCTION(bind_add_global_compile_options));

    bridge.addFunction<Component&, const std::string&>("create_executable", TO_FUNCTION(bind_create_executable));
    bridge.addFunction<Component&, const std::string&>("create_library", TO_FUNCTION(bind_create_library));

    bridge.beginClass<Component>("$Component")
        .addFunction("add_sources", &Component::bind_add_sources)
        .addFunction("add_include_paths", &Component::bind_add_include_paths)
        .addFunction("add_definitions", &Component::bind_add_definitions)
        .addFunction("add_compile_options", &Component::bind_add_compile_options)
        .addFunction("set_linker_script", &Component::bind_set_linker_script)
        .addFunction("add_libraries", &Component::bind_add_library)
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

// Import
void Project::bind_import(lua_State* L) {
    auto arg_path = luabridge::LuaRef::fromStack(L, LUA_FUNCTION_ARG_BASIC_OFFSET(1, 0));
    if (!arg_path.isString()) {
        luaL_error(L,
                   "Invalid import path: type \"%s\"\n%s",
                   lua_typename(L, arg_path.type()),
                   LuaBackend::get_script_help_string(LuaBackend::HelpEntry::IMPORT));
        throw std::runtime_error("Invalid import argument");
    }

    const auto path_str = arg_path.tostring();

    std::filesystem::path fpath(path_str);
    // default import is Folder with implicit .cfxs-build
    if (fpath.extension() == "") {
        fpath /= ".cfxs-build";
    }

    const auto filename = fpath.filename();

    if (std::filesystem::path(path_str).is_relative()) {
        s_script_path_stack.push_back(std::filesystem::weakly_canonical((s_script_path_stack.back() / fpath).parent_path()));
        s_source_location_stack.push_back(std::filesystem::weakly_canonical((s_script_path_stack.back() / fpath)));
    } else {
        s_script_path_stack.push_back(std::filesystem::weakly_canonical(std::filesystem::path(fpath).parent_path()));
        s_source_location_stack.push_back(std::filesystem::weakly_canonical(std::filesystem::path(fpath)));
    }

    const auto source_location = s_script_path_stack.back() / filename;

    if (!std::filesystem::exists(source_location)) {
        luaL_error(s_MainLuaState, "File not found: \"%s\"", source_location.string().c_str());
        return;
    }

    try {
        const bool res = luaL_dofile(s_MainLuaState, source_location.string().c_str());
        if (res) {
            // get and log lua error callstack
            print_traceback(source_location);
            throw std::runtime_error("Failed to execute script");
        }
    } catch (const std::runtime_error& e) {
        Log.error("Import load failed: {}", e.what());
        throw e; // forward exception to base try block (source script executor)
    }

    s_script_path_stack.pop_back();
    s_source_location_stack.pop_back();
}

void Project::bind_import_git(lua_State* L) {
    auto arg_url    = luabridge::LuaRef::fromStack(L, LUA_FUNCTION_ARG_BASIC_OFFSET(2, 0));
    auto arg_branch = luabridge::LuaRef::fromStack(L, LUA_FUNCTION_ARG_BASIC_OFFSET(2, 1));
    if (!arg_url.isString()) {
        luaL_error(L,
                   "Invalid import external url: type \"%s\"\n%s",
                   lua_typename(L, arg_url.type()),
                   LuaBackend::get_script_help_string(LuaBackend::HelpEntry::IMPORT_GIT));
        throw std::runtime_error("Invalid import git argument");
    }
    if (!arg_branch.isString() && !arg_branch.isNil()) {
        luaL_error(L,
                   "Invalid import external branch: type \"%s\"\n%s",
                   lua_typename(L, arg_branch.type()),
                   LuaBackend::get_script_help_string(LuaBackend::HelpEntry::IMPORT_GIT));
        throw std::runtime_error("Invalid import git argument");
    }

    auto url          = arg_url.tostring();
    const auto branch = arg_branch.isNil() ? "" : arg_branch.tostring();

    if (url.size() < 4 || url.substr(url.size() - 4) != ".git") {
        url += ".git";
    }

    std::regex git_url_regex(R"(http[s]?:\/\/.+\/([\w-]+)\/([\w-]+)\.git)");
    std::smatch match;
    if (!std::regex_search(url, match, git_url_regex)) {
        luaL_error(L, "Unsupported git url: \"%s\"", url.c_str());
        throw std::runtime_error("Unsupported git url");
    }

    const auto owner = match[1].str();
    const auto name  = match[2].str();

    const auto ext_path = s_output_path / EXTERNAL_TEMP_LOCATION / (owner + "_" + name);
    const auto ext_str  = ext_path.string();

    if (std::filesystem::exists(ext_path)) {
        GIT git(ext_path);
        if (!git.is_git_repository()) {
            luaL_error(
                s_MainLuaState,
                "Failed to update repository \"%s\" at \"%s\"\nDirectory is not a git repository\nPotential fix: Delete the directory and reconfigure",
                url.c_str(),
                ext_str.c_str());
            throw std::runtime_error("Import git existing target directory is not a git repository");
        }

        if (!git.is_git_root()) {
            luaL_error(
                s_MainLuaState,
                "Failed to update repository \"%s\" at \"%s\"\nDirectory is not a git repository root directory\nPotential fix: Delete the directory and reconfigure",
                url.c_str(),
                ext_str.c_str());
            throw std::runtime_error("Import git existing target directory is not a git repository root directory");
        }

        if (git.have_changes()) {
            Log.warn("Not updating git repository \"{}\" - uncommitted changes\n    ({})", ext_path, url);
        } else {
            if (GlobalConfig::skip_git_import_update()) {
                Log.trace("Skip repository update [{}]\n    ({})", ext_path, url);
            } else {
                Log.trace("Pull repository updates [{}]\n    ({})", ext_path, url);
                // git.fetch();
                if (git.checkout(branch))
                    git.pull();
            }
        }
    } else {
        Log.trace("Clone \"{}\" to \"{}_{}\"", url, owner, name);
        const bool cloned = GIT::clone_branch(ext_path, url, branch);

        if (!cloned) {
            luaL_error(s_MainLuaState, "Failed to clone repository \"%s\" to \"%s\"", url.c_str(), ext_str.c_str());
            throw std::runtime_error("Import git clone failed");
        }
    }

    lua_pushstring(L, ext_str.c_str());
    bind_import(L);
    lua_pop(L, 1);
}

// Linker config
void Project::bind_set_linker(const std::string& linker) {
    s_linker = std::make_shared<Linker>(linker); //
}

// Component creation
Component& Project::bind_create_executable(const std::string& name) {
    // valid filename match
    if (!RegexUtils::is_valid_component_name(name)) {
        luaL_error(s_MainLuaState,
                   "Invalid executable name [%s] - name can only contain alphanumeric characters, dashes and underscores",
                   name.c_str());
        throw std::runtime_error("Invalid executable name");
    }

    auto name_it = std::find_if(s_components.begin(), s_components.end(), [&](const std::shared_ptr<Component>& comp) {
        return comp->get_name() == name;
    });
    if (name_it != s_components.end()) {
        luaL_error(
            s_MainLuaState, "Invalid executable name [%s] - component name taken (at %s)", name.c_str(), (*name_it)->get_name().c_str());
        throw std::runtime_error("Invalid executable name");
    }

    auto comp = std::make_shared<Component>(Component::Type::EXECUTABLE,
                                            name,
                                            s_source_location_stack.back(),
                                            s_script_path_stack.back(),
                                            s_output_path / BUILD_TEMP_LOCATION / name);
    s_components.push_back(comp);
    return *comp.get();
}

Component& Project::bind_create_library(const std::string& name) {
    // valid filename match
    if (!RegexUtils::is_valid_component_name(name)) {
        luaL_error(s_MainLuaState,
                   "Invalid library name [%s] - name can only contain alphanumeric characters, dashes and underscores",
                   name.c_str());
        throw std::runtime_error("Invalid library name");
    }

    auto name_it = std::find_if(s_components.begin(), s_components.end(), [&](const std::shared_ptr<Component>& comp) {
        return comp->get_name() == name;
    });
    if (name_it != s_components.end()) {
        luaL_error(
            s_MainLuaState, "Invalid library name [%s] - component name taken (at %s)", name.c_str(), (*name_it)->get_name().c_str());
        throw std::runtime_error("Invalid library name");
    }

    auto comp = std::make_shared<Component>(Component::Type::LIBRARY,
                                            name,
                                            s_source_location_stack.back(),
                                            s_script_path_stack.back(),
                                            s_output_path / BUILD_TEMP_LOCATION / name);
    s_components.push_back(comp);
    return *comp.get();
}

void Project::bind_add_global_include_paths(lua_State* L) {
    const auto arg_sources = luabridge::LuaRef::fromStack(L, LUA_FUNCTION_ARG_BASIC_OFFSET(1, 0));
    if (arg_sources.isTable()) {
        for (int i = 1; i <= arg_sources.length(); i++) {
            auto src = arg_sources.rawget(i);
            if (src.isString()) {
                s_global_include_paths.emplace_back(src.tostring());
            } else {
                luaL_error(L, "Include directory #%d is not a string [%s]", i, lua_typename(L, src.type()));
                throw std::runtime_error("Include directory is not a string");
            }
        }
    } else if (arg_sources.isString()) {
        s_global_include_paths.emplace_back(arg_sources.tostring());
    } else {
        luaL_error(L,
                   "Invalid include paths argument: type \"%s\"\n%s",
                   lua_typename(L, arg_sources.type()),
                   LuaBackend::get_script_help_string(LuaBackend::HelpEntry::GLOBAL_ADD_INCLUDE_PATHS));
        throw std::runtime_error("Invalid include paths argument");
    }

    for (auto& dir : s_global_include_paths) {
        // check if dir is relative path
        if (dir.is_relative()) {
            // if relative path, make it absolute and canonical
            dir = std::filesystem::weakly_canonical(s_script_path_stack.back() / dir);
        } else {
            // make canonical
            dir = std::filesystem::weakly_canonical(dir);
        }
    }

    // remove duplicates
    // std::sort(m_include_paths.begin(), m_include_paths.end());
    // m_include_paths.erase(std::unique(m_include_paths.begin(), m_include_paths.end()), m_include_paths.end());
}

void Project::bind_add_global_definitions(lua_State* L) {
    auto arg_sources = luabridge::LuaRef::fromStack(L, LUA_FUNCTION_ARG_BASIC_OFFSET(1, 0));
    if (arg_sources.isTable()) {
        for (int i = 1; i <= arg_sources.length(); i++) {
            auto src = arg_sources.rawget(i);
            if (src.isString()) {
                s_global_definitions.emplace_back(src.tostring());
            } else {
                luaL_error(L, "Definition #%d is not a string [%s]", i, lua_typename(L, src.type()));
                throw std::runtime_error("Definition is not a string");
            }
        }
    } else if (arg_sources.isString()) {
        s_global_definitions.emplace_back(arg_sources.tostring());
    } else {
        luaL_error(L,
                   "Invalid definitions argument: type \"%s\"\n%s",
                   lua_typename(L, arg_sources.type()),
                   LuaBackend::get_script_help_string(LuaBackend::HelpEntry::GLOBAL_ADD_DEFINITIONS));
        throw std::runtime_error("Invalid definitions argument");
    }

    // remove duplicates
    // std::sort(m_definitions.begin(), m_definitions.end());
    // m_definitions.erase(std::unique(m_definitions.begin(), m_definitions.end()), m_definitions.end());
}

void Project::bind_add_global_compile_options(lua_State* L) {
    const auto arg_language = luabridge::LuaRef::fromStack(L, LUA_FUNCTION_ARG_BASIC_OFFSET(2, 0));

    if (!LuaBackend::is_valid_language(arg_language)) {
        luaL_error(L,
                   "Invalid compile options language argument: type \"%s\"\n%s",
                   lua_typename(L, arg_language.type()),
                   LuaBackend::get_script_help_string(LuaBackend::HelpEntry::GLOBAL_ADD_COMPILE_OPTIONS));
        throw std::runtime_error("Invalid compile options language argument");
    }

    std::vector<std::string>* vec  = nullptr;
    std::vector<std::string>* vec2 = nullptr;
    const auto val_str             = arg_language.tostring();

    if (val_str == "C") {
        vec = &s_global_c_compile_flags;
    } else if (val_str == "C++") {
        vec = &s_global_cpp_compile_flags;
    } else if (val_str == "C/C++") {
        vec  = &s_global_asm_compile_flags;
        vec2 = &s_global_c_compile_flags;
    } else if (val_str == "ASM") {
        vec = &s_global_asm_compile_flags;
    }

    if (!vec) {
        throw std::runtime_error("Add global compile definitions - invalid language");
    }

    auto arg_sources = luabridge::LuaRef::fromStack(L, LUA_FUNCTION_ARG_BASIC_OFFSET(2, 1));
    if (arg_sources.isTable()) {
        for (int i = 1; i <= arg_sources.length(); i++) {
            auto src = arg_sources.rawget(i);
            if (src.isString()) {
                vec->emplace_back(src.tostring());
                if (vec2)
                    vec2->emplace_back(src.tostring());
            } else {
                luaL_error(L, "Compile option #%d is not a string [%s]", i, lua_typename(L, src.type()));
                throw std::runtime_error("Compile option is not a string");
            }
        }
    } else if (arg_sources.isString()) {
        vec->emplace_back(arg_sources.tostring());
        if (vec2)
            vec2->emplace_back(arg_sources.tostring());
    } else {
        luaL_error(L,
                   "Invalid compile options argument: type \"%s\"\n%s",
                   lua_typename(L, arg_sources.type()),
                   LuaBackend::get_script_help_string(LuaBackend::HelpEntry::GLOBAL_ADD_COMPILE_OPTIONS));
        throw std::runtime_error("Invalid compile options argument");
    }

    // remove duplicates
    // std::sort(m_compile_options.begin(), m_compile_options.end());
    // m_compile_options.erase(std::unique(m_compile_options.begin(), m_compile_options.end()), m_compile_options.end());
}