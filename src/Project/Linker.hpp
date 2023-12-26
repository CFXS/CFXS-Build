#include <filesystem>

class Linker {
public:
    Linker(const std::string& linker);

    const std::string& get_location() const { return m_location; }

private:
    std::string m_location;
    std::vector<std::string> m_flags;
};
