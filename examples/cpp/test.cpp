#include <iostream>
#include <urage.hpp>

int main() {
    urage::Database db("test");
    
    db.add(1, "Hello World");     // ✓ Now uses urage_add
    std::cout << db.get(1) << std::endl;
    
    db.remove(1);                  // ✓ Uses urage_delete
    
    db.put_str("player:1", "Hero"); // ✓ Uses urage_put_str
    std::cout << db.get_str("player:1") << std::endl;
    
    return 0;
}