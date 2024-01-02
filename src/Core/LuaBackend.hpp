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
    static void validate_visibility(lua_State* L, const luabridge::LuaRef& arg);
    static Component::Visibility string_to_visibility(const std::string& str);
};
