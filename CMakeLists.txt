cmake_minimum_required(VERSION 3.22)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

# Enable fetch content and disable updates
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
include(FetchContent)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(cfxs-build C CXX)

set(sources
    "src/main.cpp"
    "src/Log.cpp"
    "src/Core/Project.cpp"
    "src/Core/Component.cpp"
    "src/Core/Linker.cpp"
    "src/Core/Compiler.cpp"
    "src/Core/Archiver.cpp"
    "src/Core/SourceEntry.cpp"
    "src/Core/LuaBackend.cpp"
    "src/Core/RegexUtils.cpp"
    "src/Core/GIT.cpp"
)

add_executable(cfxs-build ${sources})

target_include_directories(cfxs-build PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_precompile_headers(cfxs-build PRIVATE
    <Log.hpp>
    <Core/GlobalConfig.hpp>
)

add_compile_definitions("CFXS_BUILD_VERSION_MAJOR=1")
add_compile_definitions("CFXS_BUILD_VERSION_MINOR=0")

if(WIN32)
    add_compile_definitions("WINDOWS_BUILD=1")
    add_compile_definitions("_HAS_CXX17=1")
else()
    # target_compile_options(cfxs-build PRIVATE "-fsanitize=address,leak,undefined")
    # target_link_options(cfxs-build PRIVATE "-fsanitize=address,leak,undefined")
    # target_compile_options(cfxs-build PRIVATE "-fsanitize=thread")
    # target_link_options(cfxs-build PRIVATE "-fsanitize=thread")
    find_package(TBB REQUIRED)
    target_link_options(cfxs-build PUBLIC "-static-libstdc++")
    target_link_libraries(cfxs-build PUBLIC TBB::tbb)
    target_compile_options(cfxs-build PRIVATE "-fdiagnostics-color=always" "-Wall" "-Wextra" "-Werror")
endif()

FetchContent_Declare(
    Lua
    GIT_REPOSITORY
    https://github.com/CFXS/Lua-CMake.git
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Lua)

set(LUABRIDGE_TESTING OFF CACHE BOOL "LuaBridge tests" FORCE)
FetchContent_Declare(
    lib_LuaBridge
    GIT_REPOSITORY
    https://github.com/vinniefalco/LuaBridge.git
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(lib_LuaBridge)

FetchContent_Declare(
    argparse
    GIT_REPOSITORY
    https://github.com/p-ranav/argparse.git
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(argparse)

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

target_link_libraries(cfxs-build PRIVATE
    lua_static
    LuaBridge
    argparse
    spdlog::spdlog $<$<BOOL:${MINGW}>:ws2_32>
)
