#include "RegexUtils.hpp"
#include <regex>

bool RegexUtils::is_valid_wildcard(const std::string& path) {
    return std::regex_match(path, std::regex(R"([^*]+\*\*?\.[^\s ]+)")); //
}

bool RegexUtils::is_valid_component_name(const std::string& path) {
    return std::regex_match(path, std::regex(R"(^[a-zA-Z0-9_\- ]+$)")); //
}
