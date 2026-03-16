# STARK C++ Bindings

Simple, modern C++ wrapper for the STARK database.

## Quick Start

```cpp
#include <stark.hpp>

int main() {
    // Open database
    stark::Database db("mydb");
    
    // Store data
    db.put(42, "Hello C++!");
    
    // Retrieve
    std::cout << db.get(42) << std::endl;
    
    return 0;
}