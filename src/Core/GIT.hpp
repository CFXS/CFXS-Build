#pragma once

#include <filesystem>
class GIT {
public:
    GIT(const std::filesystem::path& working_directory);

    // Clone specific branch to location
    static bool clone_branch(const std::filesystem::path& target, const std::string& url, const std::string& branch);

    // Check if working directory is a git repository
    bool is_git_repository() const;

    // Check if working directory is a git repository root
    bool is_git_root() const;

    // Check if repository has uncommitted changes
    bool have_changes() const;

    // Fetch remote
    void fetch() const;

    // Pull current branch
    void pull() const;

    // Set branch @ commit
    bool checkout(const std::string& branch) const;

    std::string get_current_branch() const;
    std::string get_current_short_hash() const;

    const std::filesystem::path& get_working_directory() const { return m_working_directory; }

private:
    std::filesystem::path m_working_directory;
};