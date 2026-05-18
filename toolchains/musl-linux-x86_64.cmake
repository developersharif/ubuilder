# CMake toolchain file: build the ubuilder launcher with musl libc so the
# resulting binary depends on nothing but the Linux syscall ABI.
#
# Usage:
#   cmake -B build-musl -S . \
#         -DCMAKE_TOOLCHAIN_FILE=toolchains/musl-linux-x86_64.cmake \
#         -DUBUILDER_STATIC=ON \
#         -DENABLE_COMPRESSION=OFF
#   cmake --build build-musl -j
#
# Prerequisites (pick one):
#   1. apt:   sudo apt-get install -y musl-tools                   # provides musl-gcc
#   2. zig:   no install needed; set MUSL_CC=`zig cc -target x86_64-linux-musl`
#
# ENABLE_COMPRESSION should usually be OFF for musl builds because most
# distros only ship libz.so (dynamic). To keep compression, build a static
# zlib first and point CMAKE_PREFIX_PATH at it.

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Prefer the explicit environment override (`MUSL_CC=…`) over musl-gcc on PATH.
if(DEFINED ENV{MUSL_CC})
    set(CMAKE_C_COMPILER   $ENV{MUSL_CC})
    set(CMAKE_CXX_COMPILER $ENV{MUSL_CXX})
else()
    find_program(MUSL_GCC musl-gcc x86_64-linux-musl-gcc)
    if(NOT MUSL_GCC)
        message(FATAL_ERROR
            "musl-linux-x86_64.cmake: no musl-gcc found on PATH.\n"
            "Install musl-tools (apt) or set MUSL_CC=\"zig cc -target x86_64-linux-musl\".")
    endif()
    set(CMAKE_C_COMPILER ${MUSL_GCC})
endif()

# Always static when using musl — the whole point is hermeticity.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
