#include "GIT.hpp"
#include <CommandUtils.hpp>
#include <filesystem>

GIT::GIT(const std::filesystem::path& working_directory) : m_working_directory(working_directory) {}

// static function
bool GIT::clone_branch(const std::filesystem::path& target, const std::string& url, const std::string& branch) {
    // clone url to target at specific branch, do a shallow clone
    const auto [exit_code, output] =
        execute_with_args("git",
                          branch.empty() ? std::vector<std::string>{"clone", "--depth", "1", "--branch", branch, url, target.string()} :
                                           std::vector<std::string>{"clone", "--depth", "1", url, target.string()});

    if (exit_code) {
        Log.error("git clone failed: {}", output);
        return false;
    }

    return true;
}

bool GIT::is_git_repository() const {
    // run git command to check if working directory is a git repo
    const auto [exit_code, output] = execute_with_args("git", {"-C", get_working_directory().string(), "rev-parse"});

    if (exit_code)
        return false;

    return output.find("fatal: not a git repository") == std::string::npos;
}

bool GIT::is_git_root() const {
    // if toplevel path is same as working directory, then working directory is the root folder
    auto [exit_code, output] = execute_with_args("git", {"-C", get_working_directory().string(), "rev-parse", "--show-toplevel"});
    if (exit_code) {
        throw std::runtime_error("git command error");
    }

    if (output.find("fatal: not a git repository") != std::string::npos) {
        return false;
    }

    if (output.size())
        output.pop_back(); // remove last newline

    return std::filesystem::equivalent(output, std::filesystem::absolute(get_working_directory()));
}

bool GIT::have_changes() const {
    // check if local branch has uncommitted changes
    const auto [exit_code, output] = execute_with_args("git", {"-C", get_working_directory().string(), "status"});
    if (exit_code) {
        Log.error("Git status failed:\n{}", output);
        throw std::runtime_error("git command error");
    }

    return output.contains("Changes not staged for commit") || output.contains("Untracked files") ||
           output.contains("no changes added to commit");
}

void GIT::fetch() const {
    // run git fetch
    const auto [exit_code, output] = execute_with_args("git", {"-C", get_working_directory().string(), "fetch"});
    if (exit_code) {
        Log.error("Git fetch failed:\n{}", output);
        throw std::runtime_error("git command error");
    }
}

void GIT::pull() const {
    // pull current branch
    const auto [exit_code, output] = execute_with_args("git", {"-C", get_working_directory().string(), "pull"});
    if (exit_code) {
        Log.error("Git pull failed:\n{}", output);
        throw std::runtime_error("git command error");
    }
}

bool GIT::checkout(const std::string& branch) const {
    if (have_changes())
        return false;

    // checkout specific branch and pull
    const auto [exit_code, output] = execute_with_args("git", {"-C", get_working_directory().string(), "checkout", branch});
    if (exit_code) {
        Log.error("Git checkout failed:\n{}", output);
        throw std::runtime_error("git command error");
    }

    return true;
}

std::string GIT::get_current_branch() const {
    // get current branch name
    auto [exit_code, output] = execute_with_args("git", {"-C", get_working_directory().string(), "rev-parse", "--abbrev-ref", "HEAD"});
    if (exit_code) {
        Log.error("Git rev-parse failed:\n{}", output);
        throw std::runtime_error("git command error");
    }

    // trim output start and end from spaces and whitespace
    output.erase(0, output.find_first_not_of(" \t\n\r\f\v"));
    output.erase(output.find_last_not_of(" \t\n\r\f\v") + 1);

    return output;
}

std::string GIT::get_current_short_hash() const {
    // get current commit hash
    auto [exit_code, output] = execute_with_args("git", {"-C", get_working_directory().string(), "rev-parse", "--short", "HEAD"});
    if (exit_code) {
        Log.error("Git rev-parse failed:\n{}", output);
        throw std::runtime_error("git command error");
    }

    // trim output start and end from spaces and whitespace
    output.erase(0, output.find_first_not_of(" \t\n\r\f\v"));
    output.erase(output.find_last_not_of(" \t\n\r\f\v") + 1);

    return output;
}
