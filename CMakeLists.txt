cmake_minimum_required(VERSION 3.10)
project(STARK VERSION 1.0.0)

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