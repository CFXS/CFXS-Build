#pragma once

class lua_State;

class Component {
public:
    enum class Type : int {
        EXECUTABLE,
        LIBRARY,
        MODULE, // C++ module
    };

public:
    Component(Type type, const std::string& name);
    ~Component();

    void add_sources(lua_State* L);

    void build();
    void clean();

    Type get_type() const { return m_type; }
    const std::string& get_name() const { return m_name; }

private:
    Type m_type;
    std::string m_name;
    std::vector<std::string> m_sources;
    std::vector<std::string> m_source_filters;
};
