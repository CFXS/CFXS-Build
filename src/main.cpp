
#include <spdlog/common.h>
#include <spdlog/spdlog.h>
#include "Log.hpp"

int main(int argc, char **argv) {
    initialize_logging();
    Log.info("CFXS Build v{}.{}", CFXS_BUILD_VERSION_MAJOR, CFXS_BUILD_VERSION_MINOR);

    return 0;
}
