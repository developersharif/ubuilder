# Core library
add_library(ubuilder_core STATIC
    core/ubuilder.c
    core/ubuilder.h
    core/json_mini.c
    core/json_mini.h
    core/config.c
    core/config.h
    core/platform_compat.c
    core/platform_compat.h
    core/sha256.c
    core/sha256.h
    runtimes/runtime_manager.c
    runtimes/runtime_manager.h
    runtimes/python_runtime.c
    runtimes/php_runtime.c
    runtimes/nodejs_runtime.c
    runtimes/runtime_builder.c
    runtimes/runtime_builder.h
    runtimes/runtime_embedder.c
    runtimes/runtime_embedder.h
    runtimes/python_builder.c
    runtimes/php_builder.c
    runtimes/nodejs_builder.c
)

# Main executable
add_executable(ubuilder
    main.c
)

target_link_libraries(ubuilder ubuilder_core)

# S8 (audit): static-link the launcher so the produced `ubuilder` binary
# does not depend on the target host's glibc/musl at runtime. Implemented
# as -static; works against either glibc-static (Debian: libc6-dev plus
# glibc-source) or a musl toolchain (see toolchains/musl-linux-x86_64.cmake).
# Honors the Apple-sandbox rule for the launcher itself.
if(UBUILDER_STATIC)
    if(MSVC)
        # /MT statically links the C runtime on MSVC.
        target_compile_options(ubuilder_core PRIVATE /MT)
        target_compile_options(ubuilder      PRIVATE /MT)
    elseif(APPLE)
        message(WARNING "UBUILDER_STATIC=ON on macOS: Apple does not ship a static libc; "
                        "you'll get partial-static at best. Prefer the musl toolchain on Linux.")
        target_link_options(ubuilder PRIVATE -static-libgcc)
    else()
        # GNU / Clang on Linux. With glibc, you need libc6-dev (Debian/Ubuntu)
        # or glibc-static (Fedora). With musl-gcc / zig cc, -static is hermetic.
        target_link_options(ubuilder PRIVATE -static)
    endif()
endif()

# Conditional ZLIB linking
if(ZLIB_FOUND AND ENABLE_COMPRESSION)
    target_link_libraries(ubuilder_core ZLIB::ZLIB)
endif()

# Platform-specific linking
if(WIN32)
    target_link_libraries(ubuilder_core kernel32 user32)
elseif(APPLE)
    target_link_libraries(ubuilder_core "-framework CoreFoundation")
else()
    target_link_libraries(ubuilder_core pthread dl)
endif()
