class Component {
public:
    enum class Type {
        EXECUTABLE,
        LIBRARY,
        MODULE, // C++ module
    };

public:
    Component(Type type);
    ~Component();

    void build();
    void clean();

    Type get_type() const { return m_type; }
    const std::string& get_name() const { return m_name; }

private:
    Type m_type;
    std::string m_name;
};
