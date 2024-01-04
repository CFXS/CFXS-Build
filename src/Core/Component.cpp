#include "Component.hpp"
#include <algorithm>
#include <chrono>
#include <exception>
#include <lua.hpp>
#include <filesystem>
#include <CommandUtils.hpp>
#include <mutex>
#include <thread>
#include <vector>
#include "Core/Compiler.hpp"
#include "Core/Linker.hpp"
#include "Core/SourceEntry.hpp"
#include "FunctionWorker.hpp"
#include "RegexUtils.hpp"

#include <lua.hpp>
#include "LuaBackend.hpp"
#include "lauxlib.h"

std::mutex s_filesystem_mutex;

Component::Component(Type type,
                     const std::string& name,
                     const std::filesystem::path& script_path,
                     const std::filesystem::path& root_path,
                     const std::filesystem::path& local_output_directory) :
    m_type(type),
    m_name(name),
    m_script_path(std::filesystem::weakly_canonical(script_path)),
    m_root_path(std::filesystem::weakly_canonical(root_path)),
    m_local_output_directory(std::filesystem::weakly_canonical(local_output_directory)) {}

Component::~Component() {}

// TODO: move to better location
std::vector<std::string> s_TempFileExtensions = {
    ".o",
    ".dep",
};

void Component::configure(std::shared_ptr<Compiler> c_compiler,
                          std::shared_ptr<Compiler> cpp_compiler,
                          std::shared_ptr<Compiler> asm_compiler,
                          [[maybe_unused]] std::shared_ptr<Linker> linker) {
    Log.info("Configure [{}]", get_name());
    const auto configure_t1 = std::chrono::high_resolution_clock::now();

    for (auto& src : m_requested_sources) {
        if (src[0] == '.') {
            src = std::filesystem::weakly_canonical(get_root_path() / src).string();
        }
    }

    struct SourceFilePath {
        std::filesystem::path path;
        bool is_external;
    };
    std::vector<SourceFilePath> source_file_paths;

    // Add requested sources to path vector
    for (const auto& path : m_requested_sources) {
        // if path contains wildcards
        if (path.contains("*")) {
            // currently the only valid wildcards are *.extension for current folder match or **.extension for recursive match
            // This regex allows only *.ext or **.ext at the end of the path, no stars in the middle
            const bool valid_wildcard = RegexUtils::is_valid_wildcard(path);

            if (!valid_wildcard) {
                Log.error("Invalid source wildcard: {}", path);
                throw std::runtime_error("Invalid source wildcard");
            }

            const bool recursive_wildcard  = container_count(path, '*') == 2;
            const auto file_path           = std::filesystem::path(path);
            const bool is_inside_root_path = file_path.parent_path().string().starts_with(get_root_path().string());

            // TODO: check if wildcard parent paths exist on filesystem
            if (recursive_wildcard) {
                if (!is_inside_root_path) {
                    Log.error("[{}] Recursive add not available for external paths: {}", get_name(), file_path);
                    throw std::runtime_error("External path recursion");
                }

                Log.trace("[{}] Recursively add {} sources from {}", get_name(), file_path.extension(), file_path.parent_path());

                // recurse file_path.parent_path and add files to source_file_paths that match file_path.extension
                for (const auto& entry : std::filesystem::recursive_directory_iterator(file_path.parent_path())) {
                    if (entry.path().extension() == file_path.extension()) {
                        source_file_paths.push_back({entry.path(), false});
                    }
                }
            } else {
                Log.trace("[{}] Add {} sources from {}", get_name(), file_path.extension(), file_path.parent_path());
                // check all files in file_path.parent_path non recursively and add files to source_file_paths that match file_path.extension
                for (const auto& entry : std::filesystem::directory_iterator(file_path.parent_path())) {
                    if (entry.path().extension() == file_path.extension()) {
                        source_file_paths.push_back({entry.path(), !is_inside_root_path});
                    }
                }
            }
        } else {
            // source is not in wildcard form
            if (std::filesystem::exists(path)) {
                const bool is_inside_root_path = std::filesystem::path(path).parent_path().string().starts_with(get_root_path().string());
                source_file_paths.push_back({path, !is_inside_root_path});
            } else {
                Log.error("[{}] Source \"{}\" not found", get_name(), path);
                throw std::runtime_error("Source not found");
            }
        }
    }

    // remove source file paths that contain the strings in filter list
    for (const auto& filter : m_requested_source_filters) {
        source_file_paths.erase(std::remove_if(source_file_paths.begin(),
                                               source_file_paths.end(),
                                               [&](const auto& sfp) {
                                                   const bool filtered = sfp.path.string().contains(filter);
                                                   if (filtered) {
                                                       Log.trace("Remove {} [filter = {}]", sfp.path, filter);
                                                   }
                                                   return filtered;
                                               }),
                                source_file_paths.end());
    }

    // create path hashes for each parent directory
    for (const auto& e : source_file_paths) {
        std::filesystem::path output_dir;
        if (e.is_external) {
            output_dir = get_local_output_directory() / "External_" / std::to_string(std::filesystem::hash_value(e.path.parent_path()));
        } else {
            output_dir = get_local_output_directory() / std::filesystem::relative(e.path.parent_path(), get_root_path());
        }

        Compiler* compiler;
        auto ext = e.path.extension().string();
        // convert ext to lower case
        std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) {
            return std::tolower(c);
        });

        if (ext == ".c") {
            compiler = c_compiler.get();
            if (!compiler) {
                Log.error("C Compiler not set");
                throw std::runtime_error("C Compiler not set");
            }
        } else if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c++") {
            compiler = cpp_compiler.get();
            if (!compiler) {
                Log.error("C++ Compiler not set");
                throw std::runtime_error("C Compiler not set");
            }
        } else if (ext == ".asm" || ext == ".s") {
            compiler = asm_compiler.get();
            if (!compiler) {
                Log.error("ASM Compiler not set");
                throw std::runtime_error("C Compiler not set");
            }
        } else {
            throw std::runtime_error("Unsupported file type");
        }

        // create compile entry
        auto compile_entry          = std::make_unique<CompileEntry>();
        compile_entry->source_entry = std::make_unique<SourceEntry>(compiler, e.path, output_dir);
        const auto& source_entry    = *compile_entry->source_entry;

        if (!std::filesystem::exists(source_entry.get_output_directory())) {
            // Generate output_directory if it does not exist
            s_filesystem_mutex.lock();
            try {
                std::filesystem::create_directories(source_entry.get_output_directory());
            } catch (const std::exception& e) {
                Log.error("Failed to create output dir [{}]: {}", source_entry.get_output_directory(), e.what());
                throw e;
            }
            s_filesystem_mutex.unlock();
        }

        // path to output build files to
        const auto output_path = source_entry.get_output_directory() / source_entry.get_source_file_path().filename().string();

        // initial args are defined from the specific compiler implementation
        compile_entry->compile_args = compiler->get_flags();

        compiler->load_compile_and_output_flags(
            compile_entry->compile_args, source_entry.get_source_file_path(), output_path); // compile and write object
        compiler->load_dependency_flags(compile_entry->compile_args, output_path);          // dependency file output

        // include paths
        for (const auto& val : get_include_paths()) {
            compiler->push_include_path(compile_entry->compile_args, val.value.string());
        }
        // compile definitions
        for (const auto& val : get_definitions()) {
            compiler->push_compile_definition(compile_entry->compile_args, val.value);
        }
        // append custom options
        for (const auto& val : get_compile_flags())
            compile_entry->compile_args.push_back(val.value);

        compile_entry->compiler = compiler;
        m_compile_entries.emplace_back(std::move(compile_entry));
    }

    const auto configure_t2 = std::chrono::high_resolution_clock::now();
    auto configure_ms       = std::chrono::duration_cast<std::chrono::milliseconds>(configure_t2 - configure_t1).count();
    Log.trace("Configure done in {:.3}s", configure_ms / 1000.0f);
}

void Component::clean() {
    Log.info("Clean [{}] @ {}", get_name(), get_local_output_directory());
    const auto clean_t1 = std::chrono::high_resolution_clock::now();

    if (!std::filesystem::exists(get_local_output_directory()))
        return;

    // recursively remove all temp files from get_local_output_directory()
    for (const auto& entry : std::filesystem::recursive_directory_iterator(get_local_output_directory())) {
        if (std::find(s_TempFileExtensions.begin(), s_TempFileExtensions.end(), entry.path().extension()) != s_TempFileExtensions.end()) {
            const bool removed = std::filesystem::remove(entry.path());
            if (!removed) {
                Log.error("Failed to delete {}", entry.path());
                throw std::runtime_error("Failed to delete file");
            } else {
                Log.trace(" - Delete {}", entry.path());
            }
        }
    }

    const auto clean_t2 = std::chrono::high_resolution_clock::now();
    auto clean_ms       = std::chrono::duration_cast<std::chrono::milliseconds>(clean_t2 - clean_t1).count();
    Log.trace("Clean done in {:.3}s", clean_ms / 1000.0f);
}

static std::pair<int, std::string> s_compile(const std::unique_ptr<CompileEntry>& ce) {
    return execute_with_args(ce->compiler->get_location(), ce->compile_args);
}

void Component::build() {
    Log.info("Build [{}]", get_name());
    const auto build_t1 = std::chrono::high_resolution_clock::now();

    auto workers = FunctionWorker::create_workers(std::thread::hardware_concurrency());

    size_t compile_entry_seq_index         = 0;     // current source entry index to compile
    std::atomic_int current_compiled_index = 1;     // currently compiled index (only for counting compiled files)
    bool error_reported                    = false; // a source has reported a failed compilation
    bool compiling                         = true;  // still trying to compile all sources

    const auto& compile_entries = get_compile_entries();

    while (compiling) {
        if (compile_entry_seq_index == compile_entries.size()) {
            // Worker assignment is done - exit compile loop and wait for workers to finish
            compiling = false;
        } else {
            const int current_index = compile_entry_seq_index;

            // loop through all workers and try to find one that is not busy
            // break if compiling stopped/done
            for (auto& w : workers) {
                if (!compiling)
                    break;
                if (w->is_busy())
                    continue;

                w->execute([this, current_index, &compile_entries, &current_compiled_index, &compiling, &error_reported]() {
                    const auto& compile_entry = compile_entries[current_index];

                    const auto t_start    = std::chrono::high_resolution_clock::now();
                    const auto [ret, msg] = s_compile(compile_entry);

                    // don't show successful outputs from commands after the first failed one
                    const bool success = ret == 0;
                    if (success && error_reported)
                        return;

                    const auto t_end           = std::chrono::high_resolution_clock::now();
                    const auto compile_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();

                    // show full source path on fail and only filename on success
                    const auto compile_unit_path = success ? compile_entry->source_entry->get_source_file_path().filename().string() :
                                                             compile_entry->source_entry->get_source_file_path().string();

                    Log.info("[{}{}/{} ({}%) {:.03f}s{}] ({}{}{}) {} {}{}{}" ANSI_RESET,
                             success ? ANSI_GREEN : ANSI_RED,
                             current_compiled_index++,
                             get_compile_entries().size(),
                             (int)(100.0f / get_compile_entries().size() * current_compiled_index),
                             compile_time_ms / 1000.0f,
                             ANSI_RESET,
                             ANSI_LIGHT_GRAY,
                             get_name(),
                             ANSI_RESET,
                             success ? (ANSI_GRAY "Compiled") : (ANSI_RED "Failed to compile" ANSI_RESET),
                             compile_unit_path,
                             msg.empty() ? "" : "\n",
                             msg);

                    if (!success) {
                        compiling      = false;
                        error_reported = true;
                    }
                });

                compile_entry_seq_index++;
                break;
            }

            std::this_thread::sleep_for(std::chrono::microseconds(250));
        }
    }

    for (auto& w : workers) {
        while (w->is_busy()) {
            std::this_thread::sleep_for(std::chrono::microseconds(250));
        }
        w->terminate();
    }

    const auto build_t2 = std::chrono::high_resolution_clock::now();
    auto build_ms       = std::chrono::duration_cast<std::chrono::milliseconds>(build_t2 - build_t1).count();
    Log.trace("Build done in {:.3}s", build_ms / 1000.0f);
}

void Component::bind_add_sources(lua_State* L) {
    const auto push_source = [&](const std::string& src) {
        if (src.length() && src[0] == '!') {
            Log.trace("[{}] Add filter: {}", get_name(), src);
            m_requested_source_filters.push_back(src.substr(1)); // remove ! prefix
        } else {
            Log.trace("[{}] Add source: {}", get_name(), src);
            m_requested_sources.push_back(src);
        }
    };

    auto arg_sources = luabridge::LuaRef::fromStack(L, LUA_FUNCTION_ARG_COMPONENT_OFFSET(0));
    if (arg_sources.isTable()) {
        for (int i = 1; i <= arg_sources.length(); i++) {
            auto src = arg_sources.rawget(i);
            if (src.isString()) {
                push_source(src.tostring());
            } else {
                luaL_error(L, "Source #%d is not a string [%s]", i, lua_typename(L, src.type()));
                throw std::runtime_error("Source is not a string");
            }
        }
    } else if (arg_sources.isString()) {
        push_source(arg_sources.tostring());
    } else {
        luaL_error(L, "Invalid sources argument: \"{}\"", lua_typename(L, arg_sources.type()));
        throw std::runtime_error("Invalid sources argument");
    }
}

void Component::bind_add_include_paths(lua_State* L) {
    const auto arg_visibility = luabridge::LuaRef::fromStack(L, LUA_FUNCTION_ARG_COMPONENT_OFFSET(0));

    if (!LuaBackend::is_valid_visibility(arg_visibility)) {
        luaL_error(L,
                   "Invalid include paths visibility argument: type \"%s\"\n%s",
                   lua_typename(L, arg_visibility.type()),
                   LuaBackend::get_script_help_string(LuaBackend::HelpEntry::COMPONENT_ADD_INCLUDE_PATHS));
        throw std::runtime_error("Invalid include paths visibility argument");
    }

    const auto arg_sources = luabridge::LuaRef::fromStack(L, LUA_FUNCTION_ARG_COMPONENT_OFFSET(1));
    if (arg_sources.isTable()) {
        for (int i = 1; i <= arg_sources.length(); i++) {
            auto src = arg_sources.rawget(i);
            if (src.isString()) {
                m_include_paths.emplace_back(LuaBackend::string_to_visibility(arg_visibility.tostring()), src.tostring());
            } else {
                luaL_error(L, "Include directory #%d is not a string [%s]", i, lua_typename(L, src.type()));
                throw std::runtime_error("Include directory is not a string");
            }
        }
    } else if (arg_sources.isString()) {
        m_include_paths.emplace_back(LuaBackend::string_to_visibility(arg_visibility.tostring()), arg_sources.tostring());
    } else {
        luaL_error(L,
                   "Invalid include paths argument: type \"%s\"\n%s",
                   lua_typename(L, arg_sources.type()),
                   LuaBackend::get_script_help_string(LuaBackend::HelpEntry::COMPONENT_ADD_INCLUDE_PATHS));
        throw std::runtime_error("Invalid include paths argument");
    }

    for (auto& dir : m_include_paths) {
        // check if dir is relative path
        if (dir.value.is_relative()) {
            // if relative path, make it absolute and canonical
            dir.value = std::filesystem::weakly_canonical(get_root_path() / dir.value);
        } else {
            // make canonical
            dir.value = std::filesystem::weakly_canonical(dir.value);
        }
    }

    // remove duplicates
    // std::sort(m_include_paths.begin(), m_include_paths.end());
    // m_include_paths.erase(std::unique(m_include_paths.begin(), m_include_paths.end()), m_include_paths.end());
}

void Component::bind_add_definitions(lua_State* L) {
    const auto arg_visibility = luabridge::LuaRef::fromStack(L, LUA_FUNCTION_ARG_COMPONENT_OFFSET(0));

    if (!LuaBackend::is_valid_visibility(arg_visibility)) {
        luaL_error(L,
                   "Invalid definitions visibility argument: type \"%s\"\n%s",
                   lua_typename(L, arg_visibility.type()),
                   LuaBackend::get_script_help_string(LuaBackend::HelpEntry::COMPONENT_ADD_DEFINITIONS));
        throw std::runtime_error("Invalid definitions visibility argument");
    }

    auto arg_sources = luabridge::LuaRef::fromStack(L, LUA_FUNCTION_ARG_COMPONENT_OFFSET(1));
    if (arg_sources.isTable()) {
        for (int i = 1; i <= arg_sources.length(); i++) {
            auto src = arg_sources.rawget(i);
            if (src.isString()) {
                m_definitions.emplace_back(LuaBackend::string_to_visibility(arg_visibility.tostring()), src.tostring());
            } else {
                luaL_error(L, "Definition #%d is not a string [%s]", i, lua_typename(L, src.type()));
                throw std::runtime_error("Definition is not a string");
            }
        }
    } else if (arg_sources.isString()) {
        m_definitions.emplace_back(LuaBackend::string_to_visibility(arg_visibility.tostring()), arg_sources.tostring());
    } else {
        luaL_error(L,
                   "Invalid definitions argument: type \"%s\"\n%s",
                   lua_typename(L, arg_sources.type()),
                   LuaBackend::get_script_help_string(LuaBackend::HelpEntry::COMPONENT_ADD_DEFINITIONS));
        throw std::runtime_error("Invalid definitions argument");
    }

    // remove duplicates
    // std::sort(m_definitions.begin(), m_definitions.end());
    // m_definitions.erase(std::unique(m_definitions.begin(), m_definitions.end()), m_definitions.end());
}

void Component::bind_add_compile_options(lua_State* L) {
    const auto arg_visibility = luabridge::LuaRef::fromStack(L, LUA_FUNCTION_ARG_COMPONENT_OFFSET(0));

    if (!LuaBackend::is_valid_visibility(arg_visibility)) {
        luaL_error(L,
                   "Invalid compile options visibility argument: type \"%s\"\n%s",
                   lua_typename(L, arg_visibility.type()),
                   LuaBackend::get_script_help_string(LuaBackend::HelpEntry::COMPONENT_ADD_COMPILE_OPTIONS));
        throw std::runtime_error("Invalid compile options visibility argument");
    }

    auto arg_sources = luabridge::LuaRef::fromStack(L, LUA_FUNCTION_ARG_COMPONENT_OFFSET(1));
    if (arg_sources.isTable()) {
        for (int i = 1; i <= arg_sources.length(); i++) {
            auto src = arg_sources.rawget(i);
            if (src.isString()) {
                m_compile_flags.emplace_back(LuaBackend::string_to_visibility(arg_visibility.tostring()), src.tostring());
            } else {
                luaL_error(L, "Compile option #%d is not a string [%s]", i, lua_typename(L, src.type()));
                throw std::runtime_error("Compile option is not a string");
            }
        }
    } else if (arg_sources.isString()) {
        m_compile_flags.emplace_back(LuaBackend::string_to_visibility(arg_visibility.tostring()), arg_sources.tostring());
    } else {
        luaL_error(L,
                   "Invalid compile options argument: type \"%s\"\n%s",
                   lua_typename(L, arg_sources.type()),
                   LuaBackend::get_script_help_string(LuaBackend::HelpEntry::COMPONENT_ADD_COMPILE_OPTIONS));
        throw std::runtime_error("Invalid compile options argument");
    }

    // remove duplicates
    // std::sort(m_compile_flags.begin(), m_compile_flags.end());
    // m_compile_flags.erase(std::unique(m_compile_flags.begin(), m_compile_flags.end()), m_compile_flags.end());
}

void Component::bind_set_linker_script(lua_State* L) {
    auto script_path = luabridge::LuaRef::fromStack(L, LUA_FUNCTION_ARG_COMPONENT_OFFSET(0));
    if (script_path.isString()) {
        Log.trace("[{}] Set linker script: {}", get_name(), script_path.tostring());
        m_linker_script_path = script_path.tostring();
    } else {
        luaL_error(L,
                   "Invalid linker script argument: type \"%s\"\n%s",
                   lua_typename(L, script_path.type()),
                   LuaBackend::get_script_help_string(LuaBackend::HelpEntry::COMPONENT_SET_LINKER_SCRIPT));
        throw std::runtime_error("Invalid linker script argument");
    }
}

void Component::add_dependency(std::shared_ptr<Component> component) {
    Log.trace("[{}] Add [{}] as dependency", get_name(), component->get_name());
    m_dependencies.push_back(component);
}
