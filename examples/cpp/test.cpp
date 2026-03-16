#include <iostream>
#include <stark.hpp>

int main() {
    stark::Database db("test");
    
    db.add(1, "Hello World");     // ✓ Now uses stark_add
    std::cout << db.get(1) << std::endl;
    
    db.remove(1);                  // ✓ Uses stark_delete
    
    db.put_str("player:1", "Hero"); // ✓ Uses stark_put_str
    std::cout << db.get_str("player:1") << std::endl;
    
    return 0;
}