cmake_minimum_required(VERSION 3.10)
project(STARK VERSION 2.0.0)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ==================== CORE LIBRARY ====================

# Only include the files you ACTUALLY have
set(CORE_SOURCES
    core/src/database_api.c
    core/src/database.c
    core/src/btree.c
    core/src/storage.c
    core/src/pager.c
    core/src/type.c
)

# Create shared library
add_library(stark SHARED ${CORE_SOURCES})
if(ENABLE_ASAN)
    target_compile_options(stark PRIVATE -fsanitize=address -g -O0)
    target_link_options(stark PRIVATE -fsanitize=address)
endif()

# Include directories for the library
target_include_directories(stark 
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/core/include   # For stark.h
    PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/core/src       # For internal headers
)

# For Windows DLL
if(WIN32)
    target_compile_definitions(stark PRIVATE STARK_BUILD_SHARED)
endif()

# ==================== CLI ====================

# Create CLI executable
add_executable(stark_cli cli/src/main.c)

# Link to the core library
target_link_libraries(stark_cli PRIVATE stark)

# Include the public API header for CLI
target_include_directories(stark_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core/include)
if(ENABLE_ASAN)
    target_compile_options(stark_cli PRIVATE -fsanitize=address -g -O0)
    target_link_options(stark_cli PRIVATE -fsanitize=address)
endif()



# ==================== C++ Bindings ====================

# Install C++ header
install(FILES ${CMAKE_SOURCE_DIR}/bindings/cpp/stark.hpp 
        DESTINATION include)

# Optionally build examples
option(BUILD_CPP_EXAMPLES "Build C++ examples" OFF)
if(BUILD_CPP_EXAMPLES)
    add_subdirectory(examples/cpp)
endif()

# ==================== INSTALL ====================

install(TARGETS stark stark_cli)
install(FILES core/include/stark.h DESTINATION include)

# ==================== TESTS ====================

option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    add_executable(run_all
        tests/run_all.c
        tests/test_header.c
        tests/test_stats.c
        tests/test_dtype.c
        tests/test_txn.c
        tests/test_compact.c
        tests/test_undefine.c
        tests/test_cursor_stale.c
        tests/test_lock.c
        tests/test_open_ex.c
        tests/test_backup.c
        tests/test_buffer_size.c
        tests/test_foreach_range.c
    )
    target_link_libraries(run_all PRIVATE stark)
    target_include_directories(run_all PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    set_target_properties(run_all PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

    add_executable(test_cpp_types tests/test_cpp_types.cpp)
    target_link_libraries(test_cpp_types PRIVATE stark)
    target_include_directories(test_cpp_types PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core/include
        ${CMAKE_CURRENT_SOURCE_DIR}/bindings/cpp)
    set_target_properties(test_cpp_types PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

    enable_testing()
    add_test(NAME run_all COMMAND run_all)
    add_test(NAME cpp_types COMMAND test_cpp_types)
endif()
