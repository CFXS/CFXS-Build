#include "Component.hpp"
#include <lua.hpp>

Component::Component(Type type, const std::string& name) : m_type(type), m_name(name) {}

Component::~Component() {}

void Component::build() { Log.info("Build {}", get_name()); }

void Component::clean() { Log.info("Clean {}", get_name()); }

void Component::add_sources(lua_State* L) {
    int add_count = 0;
    // iterate all passed arguments from ls
    int n = lua_gettop(L);
    for (int i = 1; i <= n; i++) {
        // check if argument is a table
        if (lua_istable(L, i)) {
            // iterate table
            lua_pushnil(L);
            int x = 1;
            while (lua_next(L, i) != 0) {
                // check if value is a string
                if (lua_isstring(L, -1) && !lua_isnumber(L, -1)) {
                    // add source
                    std::string src(lua_tostring(L, -1));
                    if (src.length() && src[0] == '!') {
                        Log.trace("[{}] Add filter: {}", get_name(), src);
                        m_source_filters.push_back(src);
                    } else {
                        Log.trace("[{}] Add source: {}", get_name(), src);
                        m_sources.push_back(src);
                    }
                    add_count++;
                } else {
                    luaL_error(L, "Source #%d is not a string [%s]", x, lua_typename(L, lua_type(L, -1)));
                }
                // pop value
                lua_pop(L, 1);
                x++;
            }
        }
    }
}
