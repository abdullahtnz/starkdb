# Example C++ programs
add_executable(urage_cpp_example game_example.cpp)

# Link to the main library
target_link_libraries(urage_cpp_example PRIVATE urage)

# Include the bindings directory
target_include_directories(urage_cpp_example PRIVATE 
    ${CMAKE_SOURCE_DIR}/bindings/cpp)