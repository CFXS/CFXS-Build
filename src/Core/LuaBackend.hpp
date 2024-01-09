#pragma once
#include <lua.hpp>
#include <LuaBridge/LuaBridge.h>
#include "Component.hpp"

// TODO: check why this is positive indexed and +2. Have no idea.
// access arguments of Component calls
#define LUA_FUNCTION_ARG_COMPONENT_OFFSET(arg_index) (2 + arg_index)

// access arguments of regular function calls
#define LUA_FUNCTION_ARG_BASIC_OFFSET(arg_count, arg_index) (-(arg_count) + (arg_index))

#define LuaLog(...)   Log.info("[\033[1;36mScript\033[0m] " __VA_ARGS__)
#define LuaError(...) Log.error("[\033[1;36mScript\033[0m] " __VA_ARGS__)
#define LuaWarn(...)  Log.warn("[\033[1;36mScript\033[0m] " __VA_ARGS__)

struct lua_State;
class LuaBackend {
public:
    enum class HelpEntry {
        IMPORT,
        IMPORT_GIT,
        SET_LINKER,
        COMPONENT_ADD_INCLUDE_PATHS,
        COMPONENT_ADD_DEFINITIONS,
        COMPONENT_ADD_COMPILE_OPTIONS,
        COMPONENT_ADD_LINK_OPTIONS,
        COMPONENT_SET_LINKER_SCRIPT,
        GLOBAL_ADD_INCLUDE_PATHS,
        GLOBAL_ADD_DEFINITIONS,
        GLOBAL_ADD_COMPILE_OPTIONS,
    };

public:
    static bool is_valid_visibility(const luabridge::LuaRef& arg);
    static bool is_valid_language(const luabridge::LuaRef& arg);
    static Component::Visibility string_to_visibility(const std::string& str);

    static const char* get_script_help_string(HelpEntry he);
};
