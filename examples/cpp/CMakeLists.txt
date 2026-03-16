# Example C++ programs
add_executable(stark_cpp_example game_example.cpp)

# Link to the main library
target_link_libraries(stark_cpp_example PRIVATE stark)

# Include the bindings directory
target_include_directories(stark_cpp_example PRIVATE 
    ${CMAKE_SOURCE_DIR}/bindings/cpp)