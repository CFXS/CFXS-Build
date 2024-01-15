#include "Project.hpp"
#include <filesystem>
#include <fstream>
#include <functional>
#include <LuaBridge/LuaBridge.h>
#include "Core/Archiver.hpp"
#include "Core/Component.hpp"
#include "Core/GIT.hpp"
#include "Core/GlobalConfig.hpp"
#include "lauxlib.h"
#include <lua.hpp>
#include "LuaBackend.hpp"
#include "RegexUtils.hpp"
#include "lua.h"
#include <regex>
#include <CommandUtils.hpp>
#include <FilesystemUtils.hpp>
#include <stdexcept>
#include <vector>

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
std::shared_ptr<Archiver> s_archiver;

std::vector<std::string> e_global_c_compile_options;
std::vector<std::string> e_global_cpp_compile_options;
std::vector<std::string> e_global_definitions;
std::vector<std::filesystem::path> e_global_include_paths;
std::vector<std::string> e_global_asm_compile_options;
std::vector<std::string> e_global_link_options;

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
            exit(-1);
            throw std::runtime_error("Failed to execute script");
        } else {
            for (auto& comp : s_components) {
                comp->configure(s_c_compiler, s_cpp_compiler, s_asm_compiler, s_linker, s_archiver);
            }
        }
    } catch (const std::runtime_error& e) {
        throw e;
    }

    auto c_paths   = s_c_compiler->get_stdlib_paths();
    auto cpp_paths = s_cpp_compiler->get_stdlib_paths();

    for (auto& p : c_paths)
        p = replace_string(p, "\\", "\\\\");
    for (auto& p : cpp_paths)
        p = replace_string(p, "\\", "\\\\");

    // create single compile_commands for all components in s_project_path
    if (GlobalConfig::generate_compile_commands()) {
        std::string compile_commands;
        for (const auto& comp : s_components) {
            for (const auto& obj_path : comp->get_output_object_paths()) {
                const auto cmd_path = obj_path.string() + ".txt";
                // append contents of cmd_entry to compile_commands
                if (std::filesystem::exists(cmd_path)) {
                    std::ifstream cmd_file(cmd_path);
                    std::stringstream buffer;
                    buffer << cmd_file.rdbuf();

                    std::string paths;
                    if (obj_path.filename().string().contains("cpp")) {
                        paths = path_container_to_string_with_prefix(cpp_paths, "-I");
                    } else {
                        paths = path_container_to_string_with_prefix(c_paths, "-I");
                    }

                    auto cm = replace_string(buffer.str(), "${POST_OPTIONS}", paths);
                    compile_commands += cm;
                    cmd_file.close();
                }
            }
        }
        if (compile_commands.length()) {
            // remove trailing comma
            compile_commands.pop_back();
            compile_commands.pop_back();
        }

        std::ofstream compile_commands_file(s_project_path / "cfxs_compile_commands.json");
        if (compile_commands_file.is_open()) {
            compile_commands_file << "[\n";
            compile_commands_file << compile_commands;
            compile_commands_file << "\n]";
            compile_commands_file.close();
        } else {
            Log.error("Failed to open \"{}\" for writing", s_project_path / "cfxs_compile_commands.json");
            throw std::runtime_error("Failed to open compile_commands.json for writing");
        }
    }

    const auto t2 = std::chrono::high_resolution_clock::now();
    auto ms       = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    Log.info("Project configure done in {:.3f}s", ms / 1000.0f);
}

int e_total_project_source_count = 0;
int e_current_abs_source_index   = 1;

extern uint32_t s_fmc_hits;
extern uint32_t s_fmc_misses;

void Project::build(const std::vector<std::string>& components) {
    Log.info("Build Project");
    const auto t1 = std::chrono::high_resolution_clock::now();

    std::vector<std::shared_ptr<Component>> components_to_build;

    if (std::find(components.begin(), components.end(), "*") != components.end()) {
        // reverse iterate and build components (reverse will preserve linked/added dependency availability order)
        for (auto it = s_components.rbegin(); it != s_components.rend(); ++it) {
            components_to_build.push_back(*it);
        }
    } else {
        for (auto& c : components) {
            auto comp = std::find_if(s_components.begin(), s_components.end(), [&c](const auto& comp) {
                return comp->get_name() == c;
            });
            if (comp != s_components.end()) {
                components_to_build.push_back(*comp);
            } else {
                Log.error("Component \"{}\" does not exist", c);
                throw std::runtime_error("Component does not exist");
            }
        }
    }

    e_total_project_source_count = 0;
    e_current_abs_source_index   = 1;
    for (auto& c : components_to_build) {
        e_total_project_source_count += c->get_compile_entries().size();
    }
    for (auto& c : components_to_build) {
        c->build();
    }

    const auto t2 = std::chrono::high_resolution_clock::now();
    auto ms       = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    Log.info("Project build done in {:.3f}s", ms / 1000.0f);
    Log.info("File Modified Cache [{}/{}]", s_fmc_hits, s_fmc_misses);
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
    lua_pushstring(L, "\"Windows\"");
#else
    lua_pushstring(L, "\"Unix\"");
#endif
    lua_setglobal(L, "Platform");

    // Create printf function with custom C++ side log format
    luaL_loadstring(L, R"(_G.printf = function(...) __cfxs_print(string.format(...)) end)");
    lua_pcall(L, 0, 0, 0);

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

    bridge.addFunction<void, const std::string& /*path*/, const std::string& /*std*/>("set_c_compiler", TO_FUNCTION(bind_set_c_compiler));
    bridge.addFunction<void, const std::string& /*path*/, const std::string& /*std*/>("set_cpp_compiler",
                                                                                      TO_FUNCTION(bind_set_cpp_compiler));
    bridge.addFunction<void, const std::string& /*path*/>("set_asm_compiler", TO_FUNCTION(bind_set_asm_compiler));
    bridge.addFunction<void, const std::string& /*path*/>("set_linker", TO_FUNCTION(bind_set_linker));
    bridge.addFunction<void, const std::string& /*path*/>("set_archiver", TO_FUNCTION(bind_set_archiver));

    bridge.addFunction<void, const std::string& /*version*/, const std::string& /*path*/, const std::string& /*std*/>(
        "set_c_compiler_known", TO_FUNCTION(bind_set_c_compiler_known));
    bridge.addFunction<void, const std::string& /*version*/, const std::string& /*path*/, const std::string& /*std*/>(
        "set_cpp_compiler_known", TO_FUNCTION(bind_set_cpp_compiler_known));
    bridge.addFunction<void, const std::string& /*version*/, const std::string& /*path*/>("set_asm_compiler_known",
                                                                                          TO_FUNCTION(bind_set_asm_compiler_known));
    bridge.addFunction<void, const std::string& /*version*/, const std::string& /*path*/>("set_linker_known",
                                                                                          TO_FUNCTION(bind_set_linker_known));
    bridge.addFunction<void, const std::string& /*version*/, const std::string& /*path*/>("set_archiver_known",
                                                                                          TO_FUNCTION(bind_set_archiver_known));

    bridge.addFunction<void, const std::string&>("__cfxs_print", TO_FUNCTION(bind_cfxs_print));
    bridge.addFunction<bool, const std::string&>("exists", TO_FUNCTION(bind_exists));
    bridge.addFunction<std::string, lua_State*>("get_current_directory_path", TO_FUNCTION(bind_get_current_directory_path));
    bridge.addFunction<std::string, lua_State*>("get_current_script_path", TO_FUNCTION(bind_get_current_script_path));

    bridge.addFunction<void, lua_State*>("import", TO_FUNCTION(bind_import));
    bridge.addFunction<void, lua_State*>("import_git", TO_FUNCTION(bind_import_git));

    bridge.addFunction<void, lua_State*>("add_global_include_paths", TO_FUNCTION(bind_add_global_include_paths));
    bridge.addFunction<void, lua_State*>("add_global_definitions", TO_FUNCTION(bind_add_global_definitions));
    bridge.addFunction<void, lua_State*>("add_global_compile_options", TO_FUNCTION(bind_add_global_compile_options));
    bridge.addFunction<void, lua_State*>("add_global_link_options", TO_FUNCTION(bind_add_global_link_options));

    bridge.addFunction<Component&, const std::string&>("create_executable", TO_FUNCTION(bind_create_executable));
    bridge.addFunction<Component&, const std::string&>("create_library", TO_FUNCTION(bind_create_library));

    bridge.beginClass<Component>("$Component")
        .addFunction("add_sources", &Component::bind_add_sources)
        .addFunction("add_include_paths", &Component::bind_add_include_paths)
        .addFunction("add_definitions", &Component::bind_add_definitions)
        .addFunction("add_compile_options", &Component::bind_add_compile_options)
        .addFunction("set_linker_script", &Component::bind_set_linker_script)
        .addFunction("add_libraries", &Component::bind_add_libraries)
        .addFunction("add_link_options", &Component::bind_add_link_options)
        .addFunction("create_precompiled_header", &Component::bind_create_precompiled_header)
        .addFunction("set_compile_option_replacement", &Component::bind_set_compile_option_replacement)
        .endClass();
}

void Project::bind_cfxs_print(const std::string& str) {
    if (GlobalConfig::log_script_printf_locations()) {
        const auto current_script = s_source_location_stack.back();

        // get current code line number from s_MainLuaState
        lua_Debug ar;
        lua_getstack(s_MainLuaState, 2, &ar); // 2 instead of 1 to skip print argument at top of stack
        lua_getinfo(s_MainLuaState, "nSl", &ar);
        const auto line = ar.currentline;

        Log.info(ANSI_GRAY "<{}:{}>:" ANSI_RESET, current_script, line);
    }

    Log.info("[" ANSI_MAGENTA "Script" ANSI_RESET "] {}", str);
}

bool Project::bind_exists(const std::string& path_str) {
    const std::filesystem::path path = path_str;
    const auto p                     = path.is_relative() ? s_script_path_stack.back() / path : path;
    return std::filesystem::exists(p);
}

std::string Project::bind_get_current_directory_path(lua_State*) { return s_script_path_stack.back().string(); }

std::string Project::bind_get_current_script_path(lua_State*) { return s_source_location_stack.back().string(); }

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

void Project::bind_set_c_compiler_known(const std::string& version, const std::string& compiler, const std::string& standard) {
    s_c_compiler = std::make_shared<Compiler>(Compiler::Language::C, compiler, standard, true, version); //
}

void Project::bind_set_cpp_compiler_known(const std::string& version, const std::string& compiler, const std::string& standard) {
    s_cpp_compiler = std::make_shared<Compiler>(Compiler::Language::CPP, compiler, standard, true, version); //
}

void Project::bind_set_asm_compiler_known(const std::string& version, const std::string& compiler) {
    s_asm_compiler = std::make_shared<Compiler>(Compiler::Language::ASM, compiler, "ASM", true, version); //
}

// Import
void Project::bind_import(lua_State* L) {
    const auto arg_count       = lua_gettop(L);
    const auto extra_arg_count = arg_count - 1;

    auto arg_path = luabridge::LuaRef::fromStack(L, LUA_FUNCTION_ARG_BASIC_OFFSET(arg_count, 0));
    if (!arg_path.isString()) {
        luaL_error(L,
                   "Invalid import path: type \"%s\"\n%s",
                   lua_typename(L, arg_path.type()),
                   LuaBackend::get_script_help_string(LuaBackend::HelpEntry::IMPORT));
        throw std::runtime_error("Invalid import argument");
    }

    const auto path_str = arg_path.tostring();

    std::filesystem::path fpath(path_str);
    // default import is Folder containing .cfxs-build
    if (fpath.extension() == "") {
        fpath /= ".cfxs-build";
    }

    const auto filename = fpath.filename();

    if (std::filesystem::path(path_str).is_relative()) {
        s_script_path_stack.push_back(std::filesystem::weakly_canonical((s_script_path_stack.back() / fpath).parent_path()));
        s_source_location_stack.push_back(s_script_path_stack.back() / filename);
    } else {
        s_script_path_stack.push_back(std::filesystem::weakly_canonical(std::filesystem::path(fpath).parent_path()));
        s_source_location_stack.push_back(s_script_path_stack.back() / filename);
    }

    const auto source_location = s_source_location_stack.back();

    if (!std::filesystem::exists(source_location)) {
        luaL_error(s_MainLuaState, "File not found: \"%s\"", source_location.string().c_str());
        s_script_path_stack.pop_back();
        s_source_location_stack.pop_back();
        return;
    }

    // check if s_source_location_stack contains source_location (exclusing last entry)
    const auto it = std::find_if(s_source_location_stack.begin(), s_source_location_stack.end() - 1, [&](const auto& path) {
        return path == source_location;
    });
    if (it != s_source_location_stack.end() - 1) {
        luaL_error(s_MainLuaState,
                   "Recursive import detected: \"%s\" -> \"%s\"",
                   s_source_location_stack.back().string().c_str(),
                   source_location.string().c_str());
        throw std::runtime_error("recursive import cycle");
    }

    try {
        const bool failed = luaL_dofile_with_n_args(s_MainLuaState, source_location.string().c_str(), extra_arg_count, [&]() {
            if (extra_arg_count) {
                if (extra_arg_count > 1) {
                    lua_pushstring(s_MainLuaState, "Currently only 1 import argument is supported");
                    return 1; // error
                }
                // swap argument and function
                lua_insert(s_MainLuaState, -2); // move function to top
            }
            return 0;
        });
        if (failed) {
            // get and log lua error callstack
            print_traceback(source_location);
            exit(-1);
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
    const auto arg_count = lua_gettop(L);

    auto arg_url    = luabridge::LuaRef::fromStack(L, LUA_FUNCTION_ARG_BASIC_OFFSET(arg_count, 0));
    auto arg_branch = luabridge::LuaRef::fromStack(L, LUA_FUNCTION_ARG_BASIC_OFFSET(arg_count, 1));

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

    auto url = arg_url.tostring();
    // trim spaces from start and end
    url = url.substr(url.find_first_not_of(" \t"));
    url = url.substr(0, url.find_last_not_of(" \t") + 1);

    const auto branch = arg_branch.isNil() ? "" : arg_branch.tostring();

    if (url.length() < 4 || url.substr(url.length() - 4) != ".git") {
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

        if (GlobalConfig::skip_git_import_update()) {
            Log.trace("Skip repository update [{}]\n    ({})", ext_path, url);
        } else {
            if (git.have_changes()) {
                Log.warn("Not updating git repository \"{}\" - uncommitted changes\n    ({})", ext_path, url);
            } else {
                Log.trace("Pull repository updates [{}]\n    ({})", ext_path, url);
                // git.fetch();
                if (git.checkout(branch))
                    git.pull();
            }
        }
    } else {
        Log.info("Clone \"{}\" to \"{}_{}\"", url, owner, name);
        const bool cloned = GIT::clone_branch(ext_path, url, branch);

        if (!cloned) {
            luaL_error(s_MainLuaState, "Failed to clone repository \"%s\" to \"%s\"", url.c_str(), ext_str.c_str());
            throw std::runtime_error("Import git clone failed");
        }
    }

    if (arg_count > 2) {
        lua_insert(L, -3);                  // move arg to top
        lua_settop(L, 1);                   // cut everything except arg
        lua_pushstring(L, ext_str.c_str()); // push path
        lua_insert(L, -2);                  // move path to top
    } else {
        lua_settop(L, 0);                   // cut everything
        lua_pushstring(L, ext_str.c_str()); // push path
    }
    // call import(path, [arg])
    bind_import(L);
}

// Linker config
void Project::bind_set_linker(const std::string& linker) {
    s_linker = std::make_shared<Linker>(linker); //
}

void Project::bind_set_archiver(const std::string& ar) {
    s_archiver = std::make_shared<Archiver>(ar); //
}

void Project::bind_set_linker_known(const std::string& version, const std::string& linker) {
    s_linker = std::make_shared<Linker>(linker, true, version); //
}

void Project::bind_set_archiver_known(const std::string& version, const std::string& ar) {
    s_archiver = std::make_shared<Archiver>(ar, true, version); //
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
    const auto arg_count = lua_gettop(L);

    const auto arg_sources = luabridge::LuaRef::fromStack(L, LUA_FUNCTION_ARG_BASIC_OFFSET(arg_count, 0));
    if (arg_sources.isTable()) {
        for (int i = 1; i <= arg_sources.length(); i++) {
            auto src = arg_sources.rawget(i);
            if (src.isString()) {
                e_global_include_paths.emplace_back(src.tostring());
            } else {
                luaL_error(L, "Include directory #%d is not a string [%s]", i, lua_typename(L, src.type()));
                throw std::runtime_error("Include directory is not a string");
            }
        }
    } else if (arg_sources.isString()) {
        e_global_include_paths.emplace_back(arg_sources.tostring());
    } else {
        luaL_error(L,
                   "Invalid include paths argument: type \"%s\"\n%s",
                   lua_typename(L, arg_sources.type()),
                   LuaBackend::get_script_help_string(LuaBackend::HelpEntry::GLOBAL_ADD_INCLUDE_PATHS));
        throw std::runtime_error("Invalid include paths argument");
    }

    for (auto& dir : e_global_include_paths) {
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
    const auto arg_count = lua_gettop(L);

    auto arg_sources = luabridge::LuaRef::fromStack(L, LUA_FUNCTION_ARG_BASIC_OFFSET(arg_count, 0));
    if (arg_sources.isTable()) {
        for (int i = 1; i <= arg_sources.length(); i++) {
            auto src = arg_sources.rawget(i);
            if (src.isString()) {
                e_global_definitions.emplace_back(src.tostring());
            } else {
                luaL_error(L, "Definition #%d is not a string [%s]", i, lua_typename(L, src.type()));
                throw std::runtime_error("Definition is not a string");
            }
        }
    } else if (arg_sources.isString()) {
        e_global_definitions.emplace_back(arg_sources.tostring());
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
    const auto arg_count = lua_gettop(L);

    const auto arg_language = luabridge::LuaRef::fromStack(L, LUA_FUNCTION_ARG_BASIC_OFFSET(arg_count, 0));

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
        vec = &e_global_c_compile_options;
    } else if (val_str == "C++") {
        vec = &e_global_cpp_compile_options;
    } else if (val_str == "C/C++") {
        vec  = &e_global_c_compile_options;
        vec2 = &e_global_cpp_compile_options;
    } else if (val_str == "ASM") {
        vec = &e_global_asm_compile_options;
    }

    if (!vec) {
        throw std::runtime_error("Add global compile definitions - invalid language");
    }

    auto arg_sources = luabridge::LuaRef::fromStack(L, LUA_FUNCTION_ARG_BASIC_OFFSET(arg_count, 1));
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

void Project::bind_add_global_link_options(lua_State* L) {
    const auto arg_count = lua_gettop(L);

    auto arg_sources = luabridge::LuaRef::fromStack(L, LUA_FUNCTION_ARG_BASIC_OFFSET(arg_count, 0));
    if (arg_sources.isTable()) {
        for (int i = 1; i <= arg_sources.length(); i++) {
            auto src = arg_sources.rawget(i);
            if (src.isString()) {
                e_global_link_options.emplace_back(src.tostring());
            } else {
                luaL_error(L, "Link option #%d is not a string [%s]", i, lua_typename(L, src.type()));
                throw std::runtime_error("Link option is not a string");
            }
        }
    } else if (arg_sources.isString()) {
        e_global_link_options.emplace_back(arg_sources.tostring());
    } else {
        luaL_error(L,
                   "Invalid link options argument: type \"%s\"\n%s",
                   lua_typename(L, arg_sources.type()),
                   LuaBackend::get_script_help_string(LuaBackend::HelpEntry::GLOBAL_ADD_COMPILE_OPTIONS));
        throw std::runtime_error("Invalid link options argument");
    }

    // remove duplicates
    // std::sort(m_compile_options.begin(), m_compile_options.end());
    // m_compile_options.erase(std::unique(m_compile_options.begin(), m_compile_options.end()), m_compile_options.end());
}