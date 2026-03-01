# URAGE C++ Bindings

Simple, modern C++ wrapper for the URAGE database.

## Quick Start

```cpp
#include <urage.hpp>

int main() {
    // Open database
    urage::Database db("mydb");
    
    // Store data
    db.put(42, "Hello C++!");
    
    // Retrieve
    std::cout << db.get(42) << std::endl;
    
    return 0;
}