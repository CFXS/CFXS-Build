#include <lua.hpp>
#include "Core/LuaBackend.hpp"
#include <CommandUtils.hpp>

bool LuaBackend::is_valid_visibility(lua_State* L, const luabridge::LuaRef& arg) {
    if (arg.isString()) {
        const auto val = arg.tostring();
        if (val == "local" || val == "inherit" || val == "forward") {
            return true;
        }
    }

    return false;
}

Component::Visibility LuaBackend::string_to_visibility(const std::string& str) {
    if (str == "local") {
        return Component::Visibility::LOCAL;
    } else if (str == "inherit") {
        return Component::Visibility::INHERIT;
    } else if (str == "forward") {
        return Component::Visibility::FORWARD;
    } else {
        throw std::runtime_error("Invalid visibility argument");
    }
}

const char* LuaBackend::get_script_help_string(HelpEntry he) {
#define CODE_COLOR     ANSI_GRAY
#define FUNCTION_COLOR ANSI_MAGENTA
#define ARG_COLOR      ANSI_CYAN

// #define VISIBILITY_ARGS "\"" ANSI_YELLOW "local" ANSI_RESET "\", \"" ANSI_YELLOW "inherit" ANSI_RESET "\", " ANSI_YELLOW "forward" ANSI_RESET "\""
#define VISIBILITY_ARGS "\"local\", \"inherit\", \"forward\""

    switch (he) {
        case HelpEntry::COMPONENT_ADD_INCLUDE_PATHS:
            return "\n" ANSI_GREEN "[Usage] " CODE_COLOR "component:" FUNCTION_COLOR "add_include_paths" CODE_COLOR "(" //
                ARG_COLOR "visibility" CODE_COLOR ", "                                                                  //
                ARG_COLOR "paths" CODE_COLOR ")\n"                                                                      //
                ARG_COLOR "    visibility: " ANSI_RESET VISIBILITY_ARGS "\n"                                            //
                ARG_COLOR "    paths:      " ANSI_RESET "{\"./relative/a\", \"./relative/b\", \"/absolute/c\"}" ANSI_YELLOW
                   " or " ANSI_RESET "\"./single/path\"\n"; //
        case HelpEntry::COMPONENT_ADD_DEFINITIONS:
            return "\n" ANSI_GREEN "[Usage] " CODE_COLOR "component:" FUNCTION_COLOR "add_definitions" CODE_COLOR "(" //
                ARG_COLOR "visibility" CODE_COLOR ", "                                                                //
                ARG_COLOR "definitions" CODE_COLOR ")\n"                                                              //
                ARG_COLOR "    visibility: " ANSI_RESET VISIBILITY_ARGS "\n"                                          //
                ARG_COLOR "    definitions: " ANSI_RESET "{\"DEF_A\", \"DEF_B=0\", \"DEF_C=1\"}" ANSI_YELLOW " or " ANSI_RESET
                   "\"SINGLE_DEFINITION\"\n"; //
        case HelpEntry::COMPONENT_ADD_COMPILE_OPTIONS:
            return "\n" ANSI_GREEN "[Usage] " CODE_COLOR "component:" FUNCTION_COLOR "add_compile_options" CODE_COLOR "(" //
                ARG_COLOR "visibility" CODE_COLOR ", "                                                                    //
                ARG_COLOR "options" CODE_COLOR ")\n"                                                                      //
                ARG_COLOR "    visibility: " ANSI_RESET VISIBILITY_ARGS "\n"                                              //
                ARG_COLOR "    options:    " ANSI_RESET "{\"--option-a\", \"--option-b=3\", \"./option-c\"}" ANSI_YELLOW " or " ANSI_RESET
                   "\"--single-option\"\n"; //
        case HelpEntry::COMPONENT_SET_LINKER_SCRIPT:
            return "\n" ANSI_GREEN "[Usage] " CODE_COLOR "component:" FUNCTION_COLOR "set_linker_script" CODE_COLOR "(" //
                ARG_COLOR "path" CODE_COLOR ")\n"                                                                       //
                ARG_COLOR "    path: " ANSI_RESET "\"./path/to/linkerscript.ld\"" ANSI_GRAY " (absolute/relative)" ANSI_RESET "\n";
        case HelpEntry::SET_LINKER:
            return "\n" ANSI_GREEN "[Usage] " FUNCTION_COLOR "set_linker" CODE_COLOR "(" //
                ARG_COLOR "path" CODE_COLOR ")\n"                                        //
                ARG_COLOR "    path: " ANSI_RESET "\"linker-location\"" ANSI_GRAY " (absolute/relative path or command name)" ANSI_RESET
                   "\n";
        default: return "\n" ANSI_RED "No help available D:" ANSI_RESET "\n";
    }
}
