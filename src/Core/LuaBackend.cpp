#include <lua.hpp>
#include "Core/LuaBackend.hpp"
#include <Utils.hpp>

void LuaBackend::validate_visibility(lua_State* L, const luabridge::LuaRef& arg) {
    if (arg.isString()) {
        const auto val = arg.tostring();
        if (val == "local" || val == "inherit" || val == "forward") {
            return;
        }
    }

    luaL_error(L,
               "Invalid visibility argument: \"%s\"\nValid values: \"" ANSI_YELLOW "local" ANSI_RESET "\", \"" ANSI_YELLOW
               "inherit" ANSI_RESET "\", " ANSI_YELLOW "forward" ANSI_RESET "\"",
               arg.tostring().c_str());
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
