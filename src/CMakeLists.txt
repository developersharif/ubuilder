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
