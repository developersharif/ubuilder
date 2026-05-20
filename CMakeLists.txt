cmake_minimum_required(VERSION 3.16)
project(UBuilder VERSION 2.4.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build configuration
option(BUILD_TESTS       "Build test suite" ON)
option(ENABLE_COMPRESSION "Enable resource compression" ON)
option(ENABLE_SIGNING     "Enable code signing" OFF)
# S8 (audit): statically link the ubuilder launcher so it does not depend on
# the target host's libc at run time. Apple-sandbox rule: the CLI itself is
# part of the surface that must be hermetic. Off by default to avoid breaking
# distros that haven't installed glibc-static / musl. See
# docs/architecture/STATIC_LAUNCHER.md for the trade-offs.
option(UBUILDER_STATIC    "Statically link the ubuilder launcher (-static)" OFF)

# Platform detection
if(WIN32)
    set(PLATFORM_NAME "windows")
    add_definitions(-DPLATFORM_WINDOWS)
elseif(APPLE)
    set(PLATFORM_NAME "macos")
    add_definitions(-DPLATFORM_MACOS)
else()
    set(PLATFORM_NAME "linux")
    add_definitions(-DPLATFORM_LINUX)
endif()

# Compiler-specific options
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/src/core)
include_directories(${CMAKE_SOURCE_DIR}/src/runtimes)

# Third-party dependencies
if(ENABLE_COMPRESSION)
    find_package(ZLIB)
    if(ZLIB_FOUND)
        message(STATUS "ZLIB found: ${ZLIB_LIBRARIES}")
        add_definitions(-DHAVE_ZLIB)
    else()
        message(WARNING "ZLIB not found. Compression features will be disabled.")
        set(ENABLE_COMPRESSION OFF)
    endif()
else()
    message(STATUS "Compression disabled by user option")
endif()

# Core library
add_subdirectory(src)

# Examples
add_subdirectory(examples)

# Tests
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install configuration
install(TARGETS ubuilder
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
