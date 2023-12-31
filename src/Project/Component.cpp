#include "Component.hpp"
#include <algorithm>
#include <chrono>
#include <exception>
#include <lua.hpp>
#include <filesystem>
#include <bits/fs_path.h>
#include <re2/re2.h>
#include <Utils.hpp>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <execution>
#include "Project/SourceEntry.hpp"
#include <LuaBridge/LuaBridge.h>
#include "FunctionWorker.hpp"

Component::Component(Type type,
                     const std::string& name,
                     const std::filesystem::path& root_path,
                     const std::filesystem::path& local_output_directory) :
    m_type(type),
    m_name(name),
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
                          std::shared_ptr<Compiler> asm_compiler) {
    Log.info("Configure {}", get_name());

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
            const bool valid_wildcard = RE2::FullMatch(path, R"([^*]+\*\*?\.[^\s ]+)");

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
                    Log.error("Recursive add not available for external paths: {}", file_path);
                    throw std::runtime_error("External path recursion");
                }

                Log.trace("Recursively add {} sources from {}", file_path.extension(), file_path.parent_path());

                // recurse file_path.parent_path and add files to source_file_paths that match file_path.extension
                for (const auto& entry : std::filesystem::recursive_directory_iterator(file_path.parent_path())) {
                    if (entry.path().extension() == file_path.extension()) {
                        source_file_paths.push_back({entry.path(), false});
                    }
                }
            } else {
                Log.trace("Add {} sources from {}", file_path.extension(), file_path.parent_path());
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
                Log.error("[{}] Source \"{}\" does not exist", get_name(), path);
                throw std::runtime_error("Source does not exist");
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
        const auto ext = e.path.extension();
        if (ext == ".c") {
            compiler = c_compiler.get();
        } else if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c++") {
            compiler = cpp_compiler.get();
        } else if (ext == "asm" || ext == "S" || ext == "s") {
            compiler = asm_compiler.get();
        } else {
            throw std::runtime_error("Unsupported file type");
        }

        m_source_entries.emplace_back(compiler, e.path, output_dir);
    }
}

void Component::clean() {
    Log.info("Clean Component [{}] @ {}", get_name(), get_local_output_directory());

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
}

#define ANSI_GREEN "\033[92m"
#define ANSI_RED   "\033[91m"
#define ANSI_RESET "\033[0m"
#define ANSI_GRAY  "\033[90m"

void Component::build() {
    Log.info("Build Component [{}]", get_name());
    const auto build_t1 = std::chrono::high_resolution_clock::now();

    std::mutex fs_mutex;
    const auto compile_source = [&](const SourceEntry& se) -> auto {
        // Generate output_directory if it does not exist
        fs_mutex.lock();
        if (!std::filesystem::exists(se.get_output_directory())) {
            try {
                std::filesystem::create_directories(se.get_output_directory());
            } catch (const std::exception& e) {
                Log.error("Failed to create output dir [{}]: {}", se.get_output_directory(), e.what());
            }
        }
        fs_mutex.unlock();

        const auto obj_path        = se.get_output_directory() / se.get_source_file_path().filename().string();
        const auto dependency_path = se.get_output_directory() / se.get_source_file_path().filename().string();

        const auto* compiler          = se.get_compiler();
        std::vector<std::string> args = compiler->get_flags();

        compiler->load_compile_and_output_flags(args, se.get_source_file_path(), obj_path); // compile and write object
        compiler->load_dependency_flags(args, dependency_path);                             // dependency file output
        compiler->load_include_directories(args, get_include_directories());                // include directories
        compiler->load_compile_definitions(args, get_compile_definitions());                // compile definitions
        for (const auto& opt : get_compile_options())
            args.push_back(opt); // append custom options

        auto [compile_ret, console_out] = execute_with_args(compiler->get_location(), args);

        return std::pair<int, std::string>{compile_ret, console_out};
    };

    const int num_threads = std::thread::hardware_concurrency();

    std::vector<std::unique_ptr<FunctionWorker>> workers;
    for (int i = 0; i < num_threads; i++) {
        workers.emplace_back(std::make_unique<FunctionWorker>(i));
    }

    int se_idx          = 0;
    int comp_index      = 0;
    bool error_reported = false;
    std::mutex comp_mutex;
    bool run = true;
    while (run) {
        if (se_idx == m_source_entries.size()) {
            run = false;
        } else {
            auto se         = &m_source_entries[se_idx];
            auto* cm        = &comp_mutex;
            auto* ci        = &comp_index;
            auto* r         = &run;
            auto* got_error = &error_reported;
            for (auto& w : workers) {
                if (!w->is_busy()) {
                    w->execute([=]() {
                        const auto t1         = std::chrono::high_resolution_clock::now();
                        const auto [ret, msg] = compile_source(*se);
                        if (*got_error)
                            return;
                        const auto t2 = std::chrono::high_resolution_clock::now();
                        auto ms_int   = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
                        cm->lock();
                        (*ci)++;

                        const bool success = ret == 0;

                        std::string compile_unit_path =
                            success ? se->get_source_file_path().filename().string() : se->get_source_file_path().string();

                        Log.trace("[{}{}/{} ({}%) {:.03f}s{}] ({}) {} {}{}{}" ANSI_RESET,
                                  success ? ANSI_GREEN : ANSI_RED,
                                  *ci,
                                  m_source_entries.size(),
                                  (int)(100.0f / m_source_entries.size() * *ci),
                                  ms_int / 1000.0f,
                                  ANSI_RESET,
                                  get_name(),
                                  success ? (ANSI_GRAY "Compiled") : (ANSI_RED "Failed to compile" ANSI_RESET),
                                  compile_unit_path,
                                  msg.empty() ? "" : "\n",
                                  msg);

                        if (!success) {
                            *r         = false;
                            *got_error = true;
                        }

                        cm->unlock();
                    });
                    se_idx++;
                    break;
                }

                if (!run)
                    break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    for (auto& w : workers) {
        while (w->is_busy()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        w->terminate();
        while (w->is_running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
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

    auto arg_sources = luabridge::LuaRef::fromStack(L, 2); // offset 2 is sources list
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
        luaL_error(L, "Invalid sources argument - {}", lua_typename(L, arg_sources.type()));
        throw std::runtime_error("Invalid sources argument");
    }
}

void Component::bind_add_include_directories(lua_State* L) {
    auto arg_sources = luabridge::LuaRef::fromStack(L, 2); // offset 2 is sources list
    if (arg_sources.isTable()) {
        for (int i = 1; i <= arg_sources.length(); i++) {
            auto src = arg_sources.rawget(i);
            if (src.isString()) {
                m_include_directories.push_back(src.tostring());
            } else {
                luaL_error(L, "Include directory #%d is not a string [%s]", i, lua_typename(L, src.type()));
                throw std::runtime_error("Include directory is not a string");
            }
        }
    } else if (arg_sources.isString()) {
        m_include_directories.push_back(arg_sources.tostring());
    } else {
        luaL_error(L, "Invalid include directories argument - {}", lua_typename(L, arg_sources.type()));
        throw std::runtime_error("Invalid include directories argument");
    }

    for (std::filesystem::path& dir : m_include_directories) {
        // check if dir is relative path
        if (dir.is_relative()) {
            // if relative path, make it absolute and canonical
            dir = std::filesystem::weakly_canonical(get_root_path() / dir);
        } else {
            // make canonical
            dir = std::filesystem::weakly_canonical(dir);
        }
    }

    // remove duplicates
    // std::sort(m_include_directories.begin(), m_include_directories.end());
    // m_include_directories.erase(std::unique(m_include_directories.begin(), m_include_directories.end()), m_include_directories.end());
}

void Component::bind_add_compile_definitions(lua_State* L) {
    auto arg_sources = luabridge::LuaRef::fromStack(L, 2); // offset 2 is sources list
    if (arg_sources.isTable()) {
        for (int i = 1; i <= arg_sources.length(); i++) {
            auto src = arg_sources.rawget(i);
            if (src.isString()) {
                m_compile_definitions.push_back(src.tostring());
            } else {
                luaL_error(L, "Compile definition #%d is not a string [%s]", i, lua_typename(L, src.type()));
                throw std::runtime_error("Compile definition is not a string");
            }
        }
    } else if (arg_sources.isString()) {
        m_compile_definitions.push_back(arg_sources.tostring());
    } else {
        luaL_error(L, "Invalid compile definitions argument - {}", lua_typename(L, arg_sources.type()));
        throw std::runtime_error("Invalid compile definitions argument");
    }

    // remove duplicates
    // std::sort(m_compile_definitions.begin(), m_compile_definitions.end());
    // m_compile_definitions.erase(std::unique(m_compile_definitions.begin(), m_compile_definitions.end()), m_compile_definitions.end());
}

void Component::bind_add_compile_options(lua_State* L) {
    auto arg_sources = luabridge::LuaRef::fromStack(L, 2); // offset 2 is sources list
    if (arg_sources.isTable()) {
        for (int i = 1; i <= arg_sources.length(); i++) {
            auto src = arg_sources.rawget(i);
            if (src.isString()) {
                m_compile_options.push_back(src.tostring());
            } else {
                luaL_error(L, "Compile option #%d is not a string [%s]", i, lua_typename(L, src.type()));
                throw std::runtime_error("Compile option is not a string");
            }
        }
    } else if (arg_sources.isString()) {
        m_compile_options.push_back(arg_sources.tostring());
    } else {
        luaL_error(L, "Invalid compile options argument - {}", lua_typename(L, arg_sources.type()));
        throw std::runtime_error("Invalid compile options argument");
    }

    // remove duplicates
    // std::sort(m_compile_options.begin(), m_compile_options.end());
    // m_compile_options.erase(std::unique(m_compile_options.begin(), m_compile_options.end()), m_compile_options.end());
}

void Component::bind_set_linker_script(lua_State* L) {
    auto script_path = luabridge::LuaRef::fromStack(L, 2); // offset 2 is arg 1
    if (script_path.isString()) {
        Log.trace("[{}] Set linker script: {}", get_name(), get_linker_script_path());
        m_linker_script_path = script_path.tostring();
    } else {
        luaL_error(L, "Invalid linker script argument - {}", lua_typename(L, script_path.type()));
        throw std::runtime_error("Invalid linker script argument");
    }
}
