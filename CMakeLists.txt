cmake_minimum_required(VERSION 3.28)

# Enable fetch content and disable updates
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(CFXS_Build C CXX)

find_package(spdlog REQUIRED)

set(sources
    "src/main.cpp"
    "src/Log.cpp"
)

add_executable(CFXS_Build ${sources})

target_include_directories(CFXS_Build PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_precompile_headers(CFXS_Build PRIVATE
    <Log.hpp>
)

add_compile_definitions("CFXS_BUILD_VERSION_MAJOR=1")
add_compile_definitions("CFXS_BUILD_VERSION_MINOR=0")

FetchContent_Declare(
    Lua
    GIT_REPOSITORY
    https://github.com/CFXS/Lua-CMake.git
)
FetchContent_MakeAvailable(Lua)
target_link_libraries(CFXS_Build PRIVATE lua_static)

target_link_libraries(CFXS_Build PRIVATE spdlog::spdlog $<$<BOOL:${MINGW}>:ws2_32>)