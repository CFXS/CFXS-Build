#include "Component.hpp"
#include <algorithm>
#include <chrono>
#include <exception>
#include <lua.hpp>
#include <filesystem>
#include <CommandUtils.hpp>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include <map>
#include "Core/Archiver.hpp"
#include "Core/Compiler.hpp"
#include "Core/Linker.hpp"
#include "Core/SourceEntry.hpp"
#include "FunctionWorker.hpp"
#include "RegexUtils.hpp"
#include <fstream>

#include "LuaBackend.hpp"

////////////////////////////////////
// File modified cache
static std::map<size_t, std::filesystem::file_time_type> s_file_modified_cache;
static std::mutex s_mutex_file_modified_cache;

std::filesystem::file_time_type get_file_modified_time(const std::filesystem::path& path) {
    const auto hash = std::hash<std::string>{}(path.string());

    std::lock_guard<std::mutex> _lock(s_mutex_file_modified_cache);

    const auto it = s_file_modified_cache.find(hash);
    if (it != s_file_modified_cache.end()) {
        return it->second;
    } else {
        const auto mod_time         = std::filesystem::last_write_time(path);
        s_file_modified_cache[hash] = mod_time;
        return mod_time;
    }
}

////////////////////////////////////

static std::mutex s_filesystem_mutex;

extern std::vector<std::string> e_global_c_compile_options;
extern std::vector<std::string> e_global_cpp_compile_options;
extern std::vector<std::string> e_global_definitions;
extern std::vector<std::filesystem::path> e_global_include_paths;
extern std::vector<std::string> e_global_asm_compile_options;
extern std::vector<std::string> e_global_link_options;

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

static void prepare_and_push_flags(std::vector<std::string>& flags, const std::string& flag) {
    // escape "\" characters and split regular spaces into multiple flags
    std::vector<std::string> split_flags;
    std::string current_flag;
    bool escape_next = false;
    for (const auto& c : flag) {
        if (escape_next) {
            current_flag += c;
            escape_next = false;
        } else if (c == '\\') {
            escape_next = true;
        } else if (c == ' ') {
            split_flags.push_back(current_flag);
            current_flag.clear();
        } else {
            current_flag += c;
        }
    }
    split_flags.push_back(current_flag);
    // push to flags
    for (const auto& f : split_flags) {
        flags.push_back(f);
    }
}

static void try_merge_lib_content(Compiler* compiler,
                                  std::vector<std::string>& compile_args,
                                  const Component* lib,
                                  Component::Visibility check_visibilities) {
    // include paths
    if (lib->get_visibility_mask_include_paths() & check_visibilities) {
        for (const auto& val : lib->get_include_paths()) {
            if (!(val.visibility & check_visibilities))
                continue;
            compiler->push_include_path(compile_args, val.value.string());
        }
    }
    // compile definitions
    if (lib->get_visibility_mask_definitions() & check_visibilities) {
        for (const auto& val : lib->get_definitions()) {
            if (!(val.visibility & check_visibilities))
                continue;
            compiler->push_compile_definition(compile_args, val.value);
        }
    }
    // append custom options
    if (lib->get_visibility_mask_compile_options() & check_visibilities) {
        for (const auto& val : lib->get_compile_options()) {
            if (!(val.visibility & check_visibilities))
                continue;
            prepare_and_push_flags(compile_args, val.value);
        }
    }
}

void Component::configure(std::shared_ptr<Compiler> c_compiler,
                          std::shared_ptr<Compiler> cpp_compiler,
                          std::shared_ptr<Compiler> asm_compiler,
                          std::shared_ptr<Linker> linker,
                          std::shared_ptr<Archiver> archiver) {
    m_linker   = linker;
    m_archiver = archiver;
    Log.info("Configure [{}]", get_name());
    const auto configure_t1 = std::chrono::high_resolution_clock::now();

    for (auto& src : m_requested_sources) {
        if (src[0] == '.') {
            src = std::filesystem::weakly_canonical(get_root_path() / src).string();
        }
    }
    std::vector<SourceFilePath> source_file_paths;

    // Add requested sources to path vector
    load_source_file_paths(source_file_paths);

    // iterate all sources
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

        // path to output build files to
        const auto ts_temp     = output_dir / (e.path.filename().string() + ".tmp");
        const auto ts_dep_temp = output_dir / (e.path.filename().string() + ".dep.tmp");
        const auto obj_path    = output_dir / (e.path.filename().string() + std::string(compiler->get_object_extension()));
        const auto dep_path    = output_dir / (e.path.filename().string() + std::string(compiler->get_dependency_extension()));

        // initialize output directory for temp and build files
        if (!std::filesystem::exists(output_dir)) {
            std::filesystem::create_directories(output_dir);
        }

        bool need_build = false;

        if (!std::filesystem::exists(ts_temp) || !std::filesystem::exists(ts_dep_temp) || !std::filesystem::exists(dep_path) ||
            !std::filesystem::exists(obj_path)) {
            need_build = true;
            // create and write modify empty file ts_temp
            s_filesystem_mutex.lock();
            try {
                std::ofstream ts_temp_file(ts_temp);
                ts_temp_file.close();
                std::ofstream ts_dep_temp_file(ts_dep_temp);
                ts_dep_temp_file.close();
            } catch (const std::exception& e) {
                Log.error("[{}] Failed to create timestamp file at \"{}\": {}", get_name(), ts_temp, e.what());
                throw std::runtime_error("Failed to create timestamp file");
            }
            s_filesystem_mutex.unlock();
        } else {
            auto src_modified_time     = std::filesystem::last_write_time(e.path);  // source file
            auto ts_mark_modified_time = std::filesystem::last_write_time(ts_temp); // modified time tracker

            const bool source_modified = src_modified_time > ts_mark_modified_time;

            if (source_modified) {
                need_build = true;
                // set ts_temp write time to src_modified_time
                s_filesystem_mutex.lock();
                try {
                    std::filesystem::last_write_time(ts_temp, src_modified_time);
                } catch (const std::exception& e) {
                    Log.error("[{}] Failed to set timestamp file \"{}\" time: {}", get_name(), ts_temp, e.what());
                    throw std::runtime_error("Failed to set timestamp file time");
                }
                s_filesystem_mutex.unlock();
            } else {
                const auto ts_dep_modified_time = std::filesystem::last_write_time(ts_dep_temp);

                // iterate deps
                // parse dep_path file
                compiler->iterate_dependency_file(dep_path, [&](std::string_view path) -> bool {
                    if (e.path == path)
                        return false; // ignore "this" compile unit
                    if (!std::filesystem::exists(path))
                        return false;
                    // check if dep file is newer than obj file
                    auto dependency_modified_time = get_file_modified_time(path);
                    if (dependency_modified_time > ts_dep_modified_time) { // always check to write latest change
                        // set ts_temp write time to src_modified_time
                        s_filesystem_mutex.lock();
                        try {
                            // std::filesystem::last_write_time(ts_dep_temp, dependency_modified_time);
                            std::ofstream ts_dep_temp_file(ts_dep_temp);
                            ts_dep_temp_file.close();
                        } catch (const std::exception& e) {
                            Log.error("[{}] Failed to set timestamp file \"{}\" time: {}", get_name(), ts_temp, e.what());
                            throw std::runtime_error("Failed to set timestamp file time");
                        }
                        s_filesystem_mutex.unlock();
                        need_build = true;
                        return true; // break
                    }

                    return false; // dont break
                });
            }
        }

        if (!need_build)
            continue;

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
        compile_entry->compile_args = compiler->get_options();

        compiler->load_compile_and_output_flags(
            compile_entry->compile_args, source_entry.get_source_file_path(), output_path); // compile and write object
        compiler->load_dependency_flags(compile_entry->compile_args, output_path);          // dependency file output

        // [Local paths/definitions/options]
        // include paths
        for (const auto& val : get_include_paths()) {
            compiler->push_include_path(compile_entry->compile_args, val.value.string());
        }
        // compile definitions
        for (const auto& val : get_definitions()) {
            compiler->push_compile_definition(compile_entry->compile_args, val.value);
        }
        // append custom options
        for (const auto& val : get_compile_options()) {
            prepare_and_push_flags(compile_entry->compile_args, val.value);
        }

        // [Library paths/definitions/options]
        for (const auto* lib : get_libraries()) {
            try_merge_lib_content(compiler, compile_entry->compile_args, lib, Visibility::PUBLIC);
        }

        // Merge global defs
        // include paths
        for (const auto& val : e_global_include_paths) {
            compiler->push_include_path(compile_entry->compile_args, val.string());
        }
        // compile definitions
        for (const auto& val : e_global_definitions) {
            compiler->push_compile_definition(compile_entry->compile_args, val);
        }
        // append custom options
        std::vector<std::string>* opts = nullptr;
        switch (compiler->get_language()) {
            case Compiler::Language::C: opts = &e_global_c_compile_options; break;
            case Compiler::Language::CPP: opts = &e_global_cpp_compile_options; break;
            case Compiler::Language::ASM: opts = &e_global_asm_compile_options; break;
            default: opts = nullptr;
        }
        if (opts) {
            for (const auto& val : *opts) {
                prepare_and_push_flags(compile_entry->compile_args, val);
            }
        }

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

void Component::iterate_libs(const Component* comp, std::vector<std::string>& list) {
    for (const auto* lib : comp->get_libraries()) {
        if (lib->get_type() == Type::LIBRARY) {
            const auto lib_path =
                lib->get_local_output_directory() / (lib->get_name() + std::string(lib->m_archiver->get_archive_extension()));
            list.push_back(lib_path.string());
            iterate_libs(lib, list);
        }
    }
}

void Component::build() {
    const auto build_t1 = std::chrono::high_resolution_clock::now();

    // return if have final build object and configure did not request source build
    if (get_type() == Type::LIBRARY) {
        const auto library_path = get_local_output_directory() / (get_name() + std::string(m_archiver->get_archive_extension()));
        if (std::filesystem::exists(library_path)) {
            if (get_compile_entries().empty())
                return;
        }
    } else {
        const auto exe_path = get_local_output_directory() / (get_name() + std::string(m_linker->get_executable_extension()));
        if (std::filesystem::exists(exe_path)) {
            if (get_compile_entries().empty())
                return;
        }
    }

    const auto& compile_entries = get_compile_entries();

    if (!compile_entries.empty()) {
        Log.info("Build [{}]", get_name());

        auto workers = FunctionWorker::create_workers(GlobalConfig::number_of_worker_threads());

        size_t compile_entry_seq_index = 0; // current source entry index to compile
        int current_compiled_index     = 1; // currently compiled index (only for counting compiled files)
        std::mutex mutex_compiled_index;    // mutex for output ordering
        bool error_reported = false;        // a source has reported a failed compilation
        bool compiling      = true;         // still trying to compile all sources

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

                    w->execute([this,
                                current_index,
                                &mutex_compiled_index,
                                &compile_entries,
                                &current_compiled_index,
                                &compiling,
                                &error_reported]() {
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

                        mutex_compiled_index.lock();
                        Log.info("[{}{}/{} ({}%) {:.03f}s{}] ({}{}{}) {} {}{}{}{}" ANSI_RESET,
                                 success ? ANSI_GREEN : ANSI_RED,
                                 current_compiled_index,
                                 get_compile_entries().size(),
                                 (int)(100.0f / get_compile_entries().size() * current_compiled_index),
                                 compile_time_ms / 1000.0f,
                                 ANSI_RESET,
                                 ANSI_LIGHT_GRAY,
                                 get_name(),
                                 ANSI_RESET,
                                 success ? (ANSI_GRAY "Compiled" ANSI_GRAY) : (ANSI_RED "Failed to compile" ANSI_RESET),
                                 ANSI_GRAY,
                                 compile_unit_path,
                                 msg.empty() ? (ANSI_RESET "") : (ANSI_RESET "\n"),
                                 msg);
                        if (!success) {
                            std::string cmd;
                            for (auto& flag : compile_entry->compile_args)
                                cmd += flag + " ";
                            Log.error("command: {}", cmd);
                        }
                        current_compiled_index++;
                        mutex_compiled_index.unlock();

                        if (!success) {
                            compiling      = false;
                            error_reported = true;
                        }
                    });

                    compile_entry_seq_index++;
                    break;
                }

                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }

        for (auto& w : workers) {
            while (w->is_busy()) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            w->terminate();
        }

        if (error_reported) {
            throw std::runtime_error("Compilation failed");
        }
    }

    // Linking
    std::vector<std::filesystem::path> obj_paths;

    if (get_type() == Type::LIBRARY) {
        Log.info("Archive [{}]", get_name());
        // Create link command and execute to link all compile_entries object files into library file
        std::vector<std::string> ar_flags;
        m_archiver->load_archive_flags(ar_flags,
                                       get_local_output_directory() / (get_name() + std::string(m_archiver->get_archive_extension())));
        for (const auto& ce : get_compile_entries()) {
            obj_paths.push_back(std::filesystem::weakly_canonical(
                ce->source_entry->get_output_directory() /
                (ce->source_entry->get_source_file_path().filename().string() + std::string(ce->compiler->get_object_extension()))));
        }
        const auto arg_file = get_local_output_directory() / (get_name() + "_ar_args.txt");
        // delete arg_file
        if (std::filesystem::exists(arg_file)) {
            std::filesystem::remove(arg_file);
        }
        // write cmd_entries line by line to arg file
        std::ofstream stream_arg_file(arg_file);
        for (const auto& ce : obj_paths) {
            stream_arg_file << "\"" << ce.string() << "\""
                            << " ";
        }
        stream_arg_file.close();
        m_archiver->load_input_flag_extension_file(ar_flags, arg_file);

        const auto [ret, msg] = execute_with_args(m_archiver->get_location(), ar_flags);
        if (ret != 0) {
            Log.error("Failed to archive [{}]:\n{}", get_name(), msg);
            throw std::runtime_error("Failed to archive");
        }
    } else {
        Log.info("Link [{}]", get_name());
        // recursively iterate all libraries of get_libraries() and add .a paths to vector
        std::vector<std::string> library_paths;
        iterate_libs(this, library_paths);

        // create executable file from all lib and object files from this Component
        std::vector<std::string> link_flags;

        // expand linker script path
        if (get_linker_script_path().is_relative()) {
            // if relative path, make it absolute and canonical
            m_linker_script_path = std::filesystem::weakly_canonical(get_root_path() / m_linker_script_path);
        } else {
            // make canonical
            m_linker_script_path = std::filesystem::weakly_canonical(m_linker_script_path);
        }
        if (!std::filesystem::exists(m_linker_script_path)) {
            Log.error("[{}] Linker script not found: {}", get_name(), m_linker_script_path);
            throw std::runtime_error("Linker script not found");
        }

        m_linker->load_link_flags(link_flags,
                                  get_local_output_directory() / (get_name() + std::string(m_linker->get_executable_extension())),
                                  get_linker_script_path());

        for (const auto& ce : get_compile_entries()) {
            obj_paths.push_back(std::filesystem::weakly_canonical(
                ce->source_entry->get_output_directory() /
                (ce->source_entry->get_source_file_path().filename().string() + std::string(ce->compiler->get_object_extension()))));
        }

        const auto arg_file = get_local_output_directory() / (get_name() + "_link_args.txt");
        // delete arg_file
        if (std::filesystem::exists(arg_file)) {
            std::filesystem::remove(arg_file);
        }
        // write object paths to arg file
        std::ofstream stream_arg_file(arg_file);
        for (const auto& ce : obj_paths) {
            stream_arg_file << ce.string() << " ";
        }
        stream_arg_file.close();
        m_linker->load_input_flag_extension_file(link_flags, arg_file);

        for (const auto& lib : library_paths) {
            m_linker->load_input_flags(link_flags, lib);
        }
        for (const auto& flag : m_link_options) {
            prepare_and_push_flags(link_flags, flag);
        }

        for (const auto& flag : e_global_link_options) {
            prepare_and_push_flags(link_flags, flag);
        }

        const auto [ret, msg] = execute_with_args(m_linker->get_location(), link_flags);
        if (ret != 0) {
            Log.error("Failed to link [{}]:\n{}", get_name(), msg);
            throw std::runtime_error("Failed to link");
        }
    }

    const auto build_t2 = std::chrono::high_resolution_clock::now();
    auto build_ms       = std::chrono::duration_cast<std::chrono::milliseconds>(build_t2 - build_t1).count();
    Log.trace("Build done in {:.3}s", build_ms / 1000.0f);
}

void Component::load_source_file_paths(std::vector<SourceFilePath>& source_file_paths) {
    // Add requested paths
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

                try {
                    // recurse file_path.parent_path and add files to source_file_paths that match file_path.extension
                    for (const auto& entry : std::filesystem::recursive_directory_iterator(file_path.parent_path())) {
                        if (entry.path().extension() == file_path.extension()) {
                            source_file_paths.push_back({entry.path(), false});
                        }
                    }
                } catch (const std::exception& e) {
                    Log.error("[{}] Failed to recursively add sources from: \"{}\"\n{}", get_name(), file_path.parent_path(), e.what());
                    throw std::runtime_error("Failed to recursively add sources");
                }
            } else {
                Log.trace("[{}] Add {} sources from {}", get_name(), file_path.extension(), file_path.parent_path());
                try {
                    // check all files in file_path.parent_path non recursively and add files to source_file_paths that match file_path.extension
                    for (const auto& entry : std::filesystem::directory_iterator(file_path.parent_path())) {
                        if (entry.path().extension() == file_path.extension()) {
                            source_file_paths.push_back({entry.path(), !is_inside_root_path});
                        }
                    }
                } catch (const std::exception& e) {
                    Log.error("[{}] Failed to recursively add sources from: \"{}\"\n{}", get_name(), file_path.parent_path(), e.what());
                    throw std::runtime_error("Failed to recursively add sources");
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

    // Remove source file paths that contain the strings in filter list
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
}

void Component::bind_add_sources(lua_State* L) {
    const auto push_source = [&](const std::string& src) {
        if (src.length() && src[0] == '!') {
            // Log.trace("[{}] Add filter: {}", get_name(), src);
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
                const auto visibility_value     = LuaBackend::string_to_visibility(arg_visibility.tostring());
                m_visibility_mask_include_paths = m_visibility_mask_include_paths | visibility_value;
                m_include_paths.emplace_back(visibility_value, src.tostring());
            } else {
                luaL_error(L, "Include directory #%d is not a string [%s]", i, lua_typename(L, src.type()));
                throw std::runtime_error("Include directory is not a string");
            }
        }
    } else if (arg_sources.isString()) {
        const auto visibility_value     = LuaBackend::string_to_visibility(arg_visibility.tostring());
        m_visibility_mask_include_paths = m_visibility_mask_include_paths | visibility_value;
        m_include_paths.emplace_back(visibility_value, arg_sources.tostring());
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
                const auto visibility_value   = LuaBackend::string_to_visibility(arg_visibility.tostring());
                m_visibility_mask_definitions = m_visibility_mask_definitions | visibility_value;
                m_definitions.emplace_back(visibility_value, src.tostring());
            } else {
                luaL_error(L, "Definition #%d is not a string [%s]", i, lua_typename(L, src.type()));
                throw std::runtime_error("Definition is not a string");
            }
        }
    } else if (arg_sources.isString()) {
        const auto visibility_value   = LuaBackend::string_to_visibility(arg_visibility.tostring());
        m_visibility_mask_definitions = m_visibility_mask_definitions | visibility_value;
        m_definitions.emplace_back(visibility_value, arg_sources.tostring());
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
                const auto visibility_value       = LuaBackend::string_to_visibility(arg_visibility.tostring());
                m_visibility_mask_compile_options = m_visibility_mask_compile_options | visibility_value;
                m_compile_options.emplace_back(visibility_value, src.tostring());
            } else {
                luaL_error(L, "Compile option #%d is not a string [%s]", i, lua_typename(L, src.type()));
                throw std::runtime_error("Compile option is not a string");
            }
        }
    } else if (arg_sources.isString()) {
        const auto visibility_value       = LuaBackend::string_to_visibility(arg_visibility.tostring());
        m_visibility_mask_compile_options = m_visibility_mask_compile_options | visibility_value;
        m_compile_options.emplace_back(visibility_value, arg_sources.tostring());
    } else {
        luaL_error(L,
                   "Invalid compile options argument: type \"%s\"\n%s",
                   lua_typename(L, arg_sources.type()),
                   LuaBackend::get_script_help_string(LuaBackend::HelpEntry::COMPONENT_ADD_COMPILE_OPTIONS));
        throw std::runtime_error("Invalid compile options argument");
    }

    // remove duplicates
    // std::sort(m_compile_options.begin(), m_compile_options.end());
    // m_compile_options.erase(std::unique(m_compile_options.begin(), m_compile_options.end()), m_compile_options.end());
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

void Component::bind_add_library(lua_State* L) {
    auto arg_libs = luabridge::LuaRef::fromStack(L, LUA_FUNCTION_ARG_COMPONENT_OFFSET(0));

    if (arg_libs.isTable()) {
        for (int i = 1; i <= arg_libs.length(); i++) {
            auto lib = arg_libs.rawget(i);
            if (lib.isInstance<Component>()) {
                auto component = lib.cast<Component*>();
                if (this == component) {
                    luaL_error(L, "Component #%d - trying to add self as a library", i);
                    throw std::runtime_error("Trying to add self as a library");
                }
                if (component->get_type() != Component::Type::LIBRARY) {
                    luaL_error(L,
                               "Component #%d is not a library [\"%s\" (%s)]",
                               i,
                               component->get_name().c_str(),
                               to_string(component->get_type()));
                    throw std::runtime_error("Added component not a library");
                }
                add_library(component);
            } else {
                luaL_error(L, "Component #%d is not a valid library [%s]", i, lua_typename(L, lib.type()));
                throw std::runtime_error("Add invalid library");
            }
        }
    } else {
        if (arg_libs.isInstance<Component>()) {
            auto component = arg_libs.cast<Component*>();
            if (this == component) {
                luaL_error(L, "Trying to add self as a library");
                throw std::runtime_error("Trying to add self as a library");
            }
            if (component->get_type() != Component::Type::LIBRARY) {
                luaL_error(L, "Component is not a library [\"%s\" (%s)]", component->get_name().c_str(), to_string(component->get_type()));
                throw std::runtime_error("Added component not a library");
            }
            add_library(component);
        }
    }
}

void Component::bind_add_link_options(lua_State* L) {
    auto arg_sources = luabridge::LuaRef::fromStack(L, LUA_FUNCTION_ARG_COMPONENT_OFFSET(0));
    if (arg_sources.isTable()) {
        for (int i = 1; i <= arg_sources.length(); i++) {
            auto src = arg_sources.rawget(i);
            if (src.isString()) {
                m_link_options.emplace_back(src.tostring());
            } else {
                luaL_error(L, "Link option #%d is not a string [%s]", i, lua_typename(L, src.type()));
                throw std::runtime_error("Link option is not a string");
            }
        }
    } else if (arg_sources.isString()) {
        m_link_options.emplace_back(arg_sources.tostring());
    } else {
        luaL_error(L,
                   "Invalid link options argument: type \"%s\"\n%s",
                   lua_typename(L, arg_sources.type()),
                   LuaBackend::get_script_help_string(LuaBackend::HelpEntry::GLOBAL_ADD_COMPILE_OPTIONS));
        throw std::runtime_error("Invalid link options argument");
    }
}

// TODO: this is supposed to be a shared_ptr. Find a way to make it shared through Lua.
void Component::add_library(Component* component) {
    Log.debug("[{}] add library [{}]", get_name(), component->get_name());

    // skip duplicates
    if (std::find(get_libraries().begin(), get_libraries().end(), component) != get_libraries().end())
        return;

    // register that this component uses the added one
    component->add_user(this);
    // add to library list
    m_libraries.push_back(component);
}

void Component::add_user(Component* user) {
    // skip duplicates
    if (std::find(get_users().begin(), get_users().end(), user) != get_users().end())
        return;

    m_used_by.push_back(user);
}