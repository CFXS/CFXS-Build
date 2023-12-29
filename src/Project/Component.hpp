#pragma once
#include <lua.hpp>

class Component {
public:
    enum class Type : int {
        EXECUTABLE,
        LIBRARY,
    };

public:
    Component(Type type, const std::string& name, const std::string& root_path);
    ~Component();

    void add_sources(lua_State* L);

    void configure();
    void build();
    void clean();

    Type get_type() const { return m_type; }
    const std::string& get_name() const { return m_name; }
    const std::string& get_root_path() const { return m_root_path; }

private:
    Type m_type;
    std::string m_name;
    std::string m_root_path;
    std::vector<std::string> m_sources;
    std::vector<std::string> m_source_filters;
};
