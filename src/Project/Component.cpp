#include "Component.hpp"
#include <lua.hpp>
#include <filesystem>
#include <regex>

Component::Component(Type type, const std::string& name, const std::string& root_path) :
    m_type(type), m_name(name), m_root_path(root_path) {}

Component::~Component() {}

void Component::build() { Log.info("Build {}", get_name()); }

void Component::clean() { Log.info("Clean {}", get_name()); }

void Component::configure() {
    Log.info("Configure {}", get_name());

    // recursively iterate all files in path and get filenames
    // for (const auto& entry : std::filesystem::recursive_directory_iterator(root_path)) {
    //     if (entry.is_regular_file()) {
    //         auto filename = entry.path().filename();
    //         // get absolute path of file
    //     }
    // }
}

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
                        m_sources.push_back(src.substr(1)); // remove ! prefix
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
