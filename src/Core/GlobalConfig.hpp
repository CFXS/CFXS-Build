#pragma once

class GlobalConfig {
public:
    // Fetch/pull git imports
    // Default = false
    // Flag: --no-git-update
    static bool skip_git_import_update();
};