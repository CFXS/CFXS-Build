#include "Compiler.hpp"
#include <CommandUtils.hpp>
#include <stdexcept>
#include <fstream>

static std::string to_string(Compiler::Standard standard) {
    switch (standard) {
        case Compiler::Standard::ASM: return "ASM";
        case Compiler::Standard::C89: return "C89";
        case Compiler::Standard::C99: return "C99";
        case Compiler::Standard::C11: return "C11";
        case Compiler::Standard::C17: return "C17";
        case Compiler::Standard::C23: return "C23";
        case Compiler::Standard::CPP98: return "C++98";
        case Compiler::Standard::CPP03: return "C++03";
        case Compiler::Standard::CPP11: return "C++11";
        case Compiler::Standard::CPP14: return "C++14";
        case Compiler::Standard::CPP17: return "C++17";
        case Compiler::Standard::CPP20: return "C++20";
        case Compiler::Standard::CPP23: return "C++23";
        default: return "Unknown";
    }
}

static std::string get_standard_compile_flag(Compiler::Type type, Compiler::Standard standard) {
    if (type == Compiler::Type::GNU || type == Compiler::Type::CLANG) {
        switch (standard) {
            case Compiler::Standard::C89: return "-std=c89";
            case Compiler::Standard::C99: return "-std=c99";
            case Compiler::Standard::C11: return "-std=c11";
            case Compiler::Standard::C17: return "-std=c17";
            case Compiler::Standard::C23: return "-std=c23";
            case Compiler::Standard::CPP98: return "-std=c++98";
            case Compiler::Standard::CPP03: return "-std=c++03";
            case Compiler::Standard::CPP11: return "-std=c++11";
            case Compiler::Standard::CPP14: return "-std=c++14";
            case Compiler::Standard::CPP17: return "-std=c++17";
            case Compiler::Standard::CPP20: return "-std=c++20";
            case Compiler::Standard::CPP23: return "-std=c++23";
            default: throw std::runtime_error("Unsupported standard");
        }
    } else if (type == Compiler::Type::MSVC) {
        // This is autogenerated, some standards might not be supported
        switch (standard) {
            case Compiler::Standard::C99: return "/std:c99";
            case Compiler::Standard::C11: return "/std:c11";
            case Compiler::Standard::C17: return "/std:c17";
            case Compiler::Standard::CPP11: return "/std:c++11";
            case Compiler::Standard::CPP14: return "/std:c++14";
            case Compiler::Standard::CPP17: return "/std:c++17";
            case Compiler::Standard::CPP20: return "/std:c++20";
            default: throw std::runtime_error("Unsupported standard");
        }
    } else if (type == Compiler::Type::IAR) {
        // This is autogenerated, some standards might not be supported
        switch (standard) {
            case Compiler::Standard::CPP14: return "--c++";
            // case Compiler::Standard::CPP17: return "--cpp17"; // check IAR version
            default: throw std::runtime_error("Unsupported standard");
        }
    } else {
        throw std::runtime_error("Unsupported compiler");
    }
}

Compiler::~Compiler() { Log.trace("Delete {} Compiler", to_string(get_language())); }

Compiler::Compiler(Language language, const std::string& location, const std::string& standard_num) :
    m_language(language), m_location(location) {
    Log.trace("Create {} compiler \"{}\" with standard \"{}\"", to_string(get_language()), get_location(), standard_num);

    if (!is_valid_program(get_location())) {
        Log.error("{} Compiler \"{}\" not found", to_string(get_language()), get_location());
        throw std::runtime_error("Compiler not found");
    }

    const auto compiler_version_string = get_program_version_string(get_location());

    if (compiler_version_string.contains("GNU") || compiler_version_string.contains("gcc") || compiler_version_string.contains("g++")) {
        m_type = Type::GNU;
        m_flags.push_back("-fdiagnostics-color=always");
    } else if (compiler_version_string.contains("clang")) {
        m_type = Type::CLANG;
        m_flags.push_back("-fdiagnostics-color=always");
    } else if (compiler_version_string.contains("Microsoft")) {
        m_type = Type::MSVC;
        if (get_language() == Language::ASM) {
            throw std::runtime_error("MSVC ASM not implemented");
        }
    } else if (compiler_version_string.contains("IAR")) {
        m_type = Type::IAR;
    } else {
        Log.error("{} Compiler \"{}\" is not supported", to_string(get_language()), get_location());
        Log.info("Version:\n{}", compiler_version_string);
        throw std::runtime_error("Compiler not supported");
    }

    Log.trace(" - Type: {}", to_string(get_type()));

    if (get_language() == Language::ASM) {
        m_standard = Standard::ASM;
    } else if (get_language() == Language::C) {
        if (standard_num == "89" || standard_num == "90") {
            m_standard = Standard::C89;
        } else if (standard_num == "99") {
            m_standard = Standard::C99;
        } else if (standard_num == "11") {
            m_standard = Standard::C11;
        } else if (standard_num == "17") {
            m_standard = Standard::C17;
        } else if (standard_num == "23") {
            m_standard = Standard::C23;
        } else {
            Log.error("Unsupported C standard \"{}\"", standard_num);
            throw std::runtime_error("Unsupported C standard");
        }
    } else if (get_language() == Language::CPP) {
        if (standard_num == "98") {
            m_standard = Standard::CPP98;
        } else if (standard_num == "03") {
            m_standard = Standard::CPP03;
        } else if (standard_num == "11") {
            m_standard = Standard::CPP11;
        } else if (standard_num == "14") {
            m_standard = Standard::CPP14;
        } else if (standard_num == "17") {
            m_standard = Standard::CPP17;
        } else if (standard_num == "20") {
            m_standard = Standard::CPP20;
        } else if (standard_num == "23") {
            m_standard = Standard::CPP23;
        } else {
            Log.error("Unsupported C++ standard \"{}\"", standard_num);
            throw std::runtime_error("Unsupported C++ standard");
        }
    } else {
        Log.error("Unsupported language");
        throw std::runtime_error("Unsupported language");
    }

    Log.trace(" - Standard: {}", to_string(get_standard()));

    if (get_standard() != Standard::ASM) {
        if (get_language() == Language::C && get_type() == Type::IAR) {
            // IAR does not require flag for C
        } else {
            m_flags.push_back(get_standard_compile_flag(get_type(), get_standard()));
        }
    }
}

void Compiler::load_dependency_flags(std::vector<std::string>& flags, const std::filesystem::path& out_path) const {
    if (get_type() == Type::GNU || get_type() == Type::CLANG) {
        flags.push_back("-MMD"); // Generate header dependency list (ignore system headers, but allow user angle brackets)
        flags.push_back("-MF");  // Write to specific file
        flags.push_back(out_path.string() + ".dep");
    } else if (get_type() == Type::MSVC) {
        flags.push_back("/showIncludes"); // Generate header dependency list
        flags.push_back("/Fo");           // Write to specific file
        flags.push_back(out_path.string());
    } else if (get_type() == Type::IAR) {
        if (get_language() == Language::ASM)
            return;
        flags.push_back("--dependencies"); // Write to specific file
        flags.push_back(out_path.string() + ".dep");
    } else {
        throw std::runtime_error("Unsupported compiler");
    }
}

void Compiler::load_compile_and_output_flags(std::vector<std::string>& flags,
                                             const std::filesystem::path& source_path,
                                             const std::filesystem::path& obj_path) const {
    if (get_type() == Type::GNU || get_type() == Type::CLANG) {
        flags.push_back("-c"); // Compile only
        flags.push_back(source_path.string());
        flags.push_back("-o"); // Write to specific file
        flags.push_back(obj_path.string() + ".o");
    } else if (get_type() == Type::MSVC) {
        flags.push_back("/c");  // Compile only
        flags.push_back(source_path.string());
        flags.push_back("/Fo"); // Write to specific file
        flags.push_back(obj_path.string());
    } else if (get_type() == Type::IAR) {
        if (get_language() != Language::ASM)
            flags.push_back("--silent"); // Do not generate compile spam
        flags.push_back(source_path.string());
        flags.push_back("-o");           // Write to specific file
        flags.push_back(obj_path.string() + ".o");
    } else {
        throw std::runtime_error("Unsupported compiler");
    }
}

void Compiler::push_include_path(std::vector<std::string>& flags, const std::string& include_directory) const {
    auto inc = include_directory;
    if (inc.find(' ') != std::string::npos) {
        inc = "\\\"" + inc + "\\\"";
    }

    if (get_type() == Type::GNU || get_type() == Type::CLANG || get_type() == Type::IAR) {
        flags.push_back("-I" + inc);
    } else if (get_type() == Type::MSVC) {
        // do this for MSVC
        flags.push_back("/I");
        flags.push_back(inc);
    } else {
        throw std::runtime_error("Unsupported compiler");
    }
}

std::string replace_string(std::string subject, const std::string& search, const std::string& replace) {
    size_t pos = 0;
    while ((pos = subject.find(search, pos)) != std::string::npos) {
        subject.replace(pos, search.length(), replace);
        pos += replace.length();
    }
    return subject;
}

void Compiler::push_compile_definition(std::vector<std::string>& flags, const std::string& compile_definition) const {
    // escape "\" in compile_definition and wrap value after DEFINITION_NAME= in escaped quotes if value contains spaces
    // replace all '"' with "\\"" safely without entering an infinite loop
    // std::string escaped_compile_definition = replace_string(compile_definition, "\"", "\\\"");

    // replace all "\\" with "\\\\"
    std::string escaped_compile_definition = replace_string(compile_definition, "\\", "\\\\\\");

    auto eq_pos     = escaped_compile_definition.find('=');
    std::string def = "";
    if (eq_pos != std::string::npos) {
        auto part_2 = escaped_compile_definition.substr(eq_pos + 1);
        if (part_2.find(' ') != std::string::npos) {
            def = escaped_compile_definition.substr(0, eq_pos + 1) + "\\\"" + part_2 + "\\\"";
        } else {
            def = escaped_compile_definition.substr(0, eq_pos + 1) + part_2;
        }
    } else {
        def = escaped_compile_definition;
    }

    if (get_type() == Type::GNU || get_type() == Type::CLANG || get_type() == Type::IAR || get_type() == Type::IAR) {
        flags.push_back("-D" + def);
    } else if (get_type() == Type::MSVC) {
        flags.push_back("/D");
        flags.push_back(def);
    } else {
        throw std::runtime_error("Unsupported compiler");
    }
}

std::string_view Compiler::get_object_extension() const {
    if (get_type() == Type::GNU || get_type() == Type::CLANG) {
        return ".o";
    } else if (get_type() == Type::MSVC) {
        return ".obj";
    } else if (get_type() == Type::IAR) {
        return ".o";
    } else {
        throw std::runtime_error("Unsupported compiler");
    }
}

std::string_view Compiler::get_dependency_extension() const {
    if (get_type() == Type::GNU || get_type() == Type::CLANG || get_type() == Type::IAR) {
        return ".dep";
    } else if (get_type() == Type::MSVC) {
        throw std::runtime_error("Not implemented");
    } else {
        throw std::runtime_error("Unsupported compiler");
    }

    return "";
}

void Compiler::iterate_dependency_file(const std::filesystem::path& dependency_file,
                                       const std::function<bool(std::string_view)>& callback) const {
    std::ifstream dep_file(dependency_file);

    if (get_type() == Type::GNU || get_type() == Type::CLANG) {
        /* Format:
            object/path/obj.o: \
            dep/path/a.cpp \
            dep/path/b.hpp \
            dep/path/c.hpp \
        */
        // Format includes the compiled cpp file as well.
        // TODO: don't check the actual compiled file - other cpp files should be ok to check
        std::string line;
        std::getline(dep_file, line); // skip first line
        while (std::getline(dep_file, line)) {
            // trim line spaces from beginning and remove potential " \" at the end of the line
            const auto last_backslash = line.find_last_of('\\');
            std::string_view line_sv(line.data() + line.find_first_not_of(' '),
                                     line.data() + (last_backslash != std::string::npos ? (last_backslash - 1) : line.length()));

            const bool should_return = callback(line_sv);
            if (should_return)
                return;
        }
    } else if (get_type() == Type::IAR) {
        /* Format:
            dep/path/a.hpp
            dep/path/b.hpp
            dep/path/c.hpp
        */
        std::string line;
        while (std::getline(dep_file, line)) {
            if (line.starts_with("C:\\Program Files (x86)\\IAR Systems")) { // Skip IAR system includes
                continue;
            }
            const bool should_return = callback(line);
            if (should_return)
                return;
        }
    } else if (get_type() == Type::MSVC) {
        throw std::runtime_error("Not implemented");
    } else {
        throw std::runtime_error("Unsupported compiler");
    }
}
