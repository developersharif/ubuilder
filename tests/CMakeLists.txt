# Test executable
add_executable(test_ubuilder
    test_main.c
    test_core.c
    test_runtime_manager.c
    test_config.c
    test_platform_compat.c
    test_sha256.c
    test_php_builder.c
    test_glob_match.c
)

target_link_libraries(test_ubuilder ubuilder_core)
target_include_directories(test_ubuilder PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Add test
add_test(NAME UBuilderTests COMMAND test_ubuilder)
