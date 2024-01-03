#pragma once

class RegexUtils {
public:
    static bool is_valid_wildcard(const std::string& path);
    static bool is_valid_component_name(const std::string& name);
};