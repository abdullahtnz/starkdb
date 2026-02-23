cmake_minimum_required(VERSION 3.10)
project(URAGE VERSION 1.0.0)

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
    # utils.c is REMOVED - you don't have this file
)

# Create shared library
add_library(urage SHARED ${CORE_SOURCES})

# Include directories for the library
target_include_directories(urage 
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/core/include   # For urage.h
    PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/core/src       # For internal headers
)

# For Windows DLL
if(WIN32)
    target_compile_definitions(urage PRIVATE URAGE_BUILD_SHARED)
endif()

# ==================== CLI ====================

# Create CLI executable
add_executable(urage_cli cli/src/main.c)

# Link to the core library
target_link_libraries(urage_cli PRIVATE urage)

# Include the public API header for CLI
target_include_directories(urage_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core/include)

# ==================== INSTALL ====================

install(TARGETS urage urage_cli)
install(FILES core/include/urage.h DESTINATION include)