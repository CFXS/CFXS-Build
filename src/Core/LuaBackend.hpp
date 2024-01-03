#pragma once
#include <lua.hpp>
#include <LuaBridge/LuaBridge.h>
#include "Component.hpp"

#define LuaLog(...)   Log.info("[\033[1;36mLua\033[0m] " __VA_ARGS__)
#define LuaError(...) Log.error("[\033[1;36mLua\033[0m] " __VA_ARGS__)
#define LuaWarn(...)  Log.warn("[\033[1;36mLua\033[0m] " __VA_ARGS__)

struct lua_State;
class LuaBackend {
public:
    enum class HelpEntry {
        SET_LINKER,
        COMPONENT_ADD_INCLUDE_PATHS,
        COMPONENT_ADD_DEFINITIONS,
        COMPONENT_ADD_COMPILE_OPTIONS,
        COMPONENT_ADD_LINK_OPTIONS,
        COMPONENT_SET_LINKER_SCRIPT,
    };

public:
    static bool is_valid_visibility(lua_State* L, const luabridge::LuaRef& arg);
    static Component::Visibility string_to_visibility(const std::string& str);

    static const char* get_script_help_string(HelpEntry he);
};
