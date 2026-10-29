# STARK C++ Bindings

Simple, modern C++ wrapper for the STARK database.

Database files use STARKDB **file format version 1**: both `.idx` and `.dat`
begin with a 16-byte header (magic `"STARKDB\0"`, version, flags). Legacy
header-less databases must be upgraded with `stark_upgrade_file()` before use.

## Type definitions

`Database::define_type()` computes field byte offsets automatically; you never
supply offsets. `TYPE_INT` fields are always 4 bytes. Multi-field types defined
from C++ are laid out identically to the CLI and Python bindings, so records are
interoperable across languages.

```cpp
db.define_type("player", {
    stark::Field("a", TYPE_INT),
    stark::Field("b", TYPE_INT),
    stark::Field("c", TYPE_STRING, 16),
});
db.add_typed("player", 1, "a=100 b=50 c=hello");
```

`db.compact()` reclaims space left by overwritten/deleted keys.

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