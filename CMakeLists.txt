cmake_minimum_required(VERSION 3.22)

# Enable fetch content and disable updates
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
include(FetchContent)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(CFXS_Build C CXX)

set(sources
    "src/main.cpp"
    "src/Log.cpp"
    "src/Project/Project.cpp"
    "src/Project/Component.cpp"
    "src/Project/Linker.cpp"
    "src/Project/Compiler.cpp"
    "src/Project/SourceEntry.cpp"
)

add_executable(CFXS_Build ${sources})

target_include_directories(CFXS_Build PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_precompile_headers(CFXS_Build PRIVATE
    <Log.hpp>
)

add_compile_definitions("CFXS_BUILD_VERSION_MAJOR=1")
add_compile_definitions("CFXS_BUILD_VERSION_MINOR=0")

if(WIN32)
    add_compile_definitions("WINDOWS_BUILD=1")
    add_compile_definitions("_HAS_CXX17=1")
endif()

FetchContent_Declare(
    Lua
    GIT_REPOSITORY
    https://github.com/CFXS/Lua-CMake.git
)
FetchContent_MakeAvailable(Lua)

set(LUABRIDGE_TESTING OFF CACHE BOOL "LuaBridge tests" FORCE)
FetchContent_Declare(
    lib_LuaBridge
    GIT_REPOSITORY
    https://github.com/vinniefalco/LuaBridge.git
)
FetchContent_MakeAvailable(lib_LuaBridge)

FetchContent_Declare(
    argparse
    GIT_REPOSITORY
    https://github.com/p-ranav/argparse.git
)
FetchContent_MakeAvailable(argparse)

FetchContent_Declare(
    lib_re2
    GIT_REPOSITORY
    https://github.com/google/re2.git
    GIT_TAG "main"
)
FetchContent_MakeAvailable(lib_re2)

# set(CTRE_BUILD_TESTS OFF CACHE BOOL "Build tests" FORCE)
# set(CTRE_BUILD_PACKAGE_RPM OFF CACHE BOOL "Build tests" FORCE)
# set(CTRE_BUILD_PACKAGE_DEB OFF CACHE BOOL "Build tests" FORCE)
# FetchContent_Declare(
# lib_ctre
# GIT_REPOSITORY
# https://github.com/hanickadot/compile-time-regular-expressions.git
# )
# FetchContent_MakeAvailable(lib_ctre)

# find package spdlog, if not found then do fetchcontent
find_package(spdlog QUIET)

if(NOT spdlog_FOUND)
    message(STATUS "spdlog not found, fetching from github")
    set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "Build shared library" FORCE)
    set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL "Build examples" FORCE)
    set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "Build tests" FORCE)
    set(SPDLOG_INSTALL OFF CACHE BOOL "Generate installation target" FORCE)

    FetchContent_Declare(
        lib_spdlog
        GIT_REPOSITORY
        https://github.com/gabime/spdlog.git
        GIT_TAG "v2.x"
    )
    FetchContent_MakeAvailable(lib_spdlog)
endif()

target_link_libraries(CFXS_Build PRIVATE
    lua_static
    LuaBridge
    argparse
    re2
    spdlog::spdlog $<$<BOOL:${MINGW}>:ws2_32>
)
