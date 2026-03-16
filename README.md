# Introducing STARK v.1.1.0


# What is STARK ?
STARK is a *key-value based, b-tree implemented local* database. It is specifically designed for mobile applications, desktop applications, offline video games. With its *speed,  lightness and easy syntax*, STARK stands  among other databases.

## Workflow

STARK works with .dat and .idx files to save data. It supports *ACID properties*, ensuring save and reliable data saving. After building STARK, users can choose to add data via either STARK CLI or C++ code.

#  🪟 Windows installation

 

  

    # 1. Download STARK 
    # Go to https://github.com/abdullahtnz/stark and download the ZIP file
    # Extract it to your project folder C:\my_project
    # You must get a path like: C:\my_project\stark
    
    # 2. Open Command Prompt as Administrator
    # Press Windows Key, type "cmd", right-click, "Run as Administrator"
    
    # 3. Go to STARK folder
    cd C:\my_project\stark
    mkdir build
    cd build
    
    # 4. Build
    cmake .. -G "MinGW Makefiles" -DBUILD_SHARED=ON
    mingw32-make
    
    # 5. Install (copy to MinGW)
    copy libstark.dll C:\mingw64\bin\
    copy libstark.dll.a C:\mingw64\lib\
    copy ..\bindings\cpp\stark.hpp C:\mingw64\include\
    copy ..\core\include\stark.h C:\mingw64\include\
    
    # ✅ DONE! One-time setup complete.
<hr>

# 🐧 Linux installation

    # 1. Open Terminal (Ctrl+Alt+T) and go to your project folder
    
    cd Desktop/my_project
    
    # 2. Download and build

    git clone https://github.com/abdullahtnz/stark.git
    cd stark
    mkdir build
    cd build
    cmake .. -DBUILD_SHARED=ON
    make
    
    # 3. Install (one time)
    sudo make install
    sudo ldconfig
    
    # ✅ DONE! One-time setup complete.

<hr>

> As mentioned above, you can use stark via CLI or C++ code.

# How to use STARK in CLI

   ## Windows & Linux
   

    # Go to stark\build folder
    cd C:\my_project\build
    ./stark_cli filename  # This creates or opens a file in the build folder only.
    
    To open or create file in other direction:
    cd C:\my_project\my_folder # Go to the folder where you want your file.
    stark_cli filename

# How to use STARK in C++ 
Installation process already covers almost everything. All you need to do is simply adding **#include <stark.hpp>** and then use it with proper syntax.

    main.cpp
	   
	   #include <stark.hpp>
	   
	   int main() {
     // Open database (creates "save.idx" and "save.dat")
     stark::Database db("save");
      
     // Save player
     db.add(1, "Hero");
     db.add(2, "100 HP");
      
     // Load player
     std::cout << db.get(1) << std::endl;  // Shows: Hero
      
     return 0;
    }

> You may learn more about syntax and usage below:

# STARK CLI Documentation

 ### General commands
 

 - help -> to see all possible commands and usage
 - exit -> to save and quit
 
 ### Numeric key commands
 

> STARK handles numeric keys and string keys differently; hence, these commands exists.

    
   addn key value -> **add** numeric key with value (value can be both string and num)
   
   getn key -> **get** numeric key and its value
   deln key -> **delete** numeric key and its value
   existsn -> **check** if a numeric key exist or not

### String key commands
adds key value -> **add** string key with value (value can be both string and num)

gets key -> **gets** string key and its value

dels key -> **delete** string key and its value
exist_str -> **check** if a string key exist or not

### Type commands

> Types are user-defined, flexible structures.

define name { field1 field1's data type field2 field2's data type } -> **define** a new type

undefine name -> **delete** a type
desc name -> **see** a type and its fields

### Adding/Getting a type data
add typename key field1=value field2=value -> **add** item as type

get typename key -> **get** a type's specific item

<hr>

## To understand types better, here is how you can use it.

 

       # 1. Open your database
    stark_cli mygame
    
    # 2. Define a player type
    stark> define player {
        name string(32) # 32 here is the size of string
        hp int
        level int
        gold int
        class string(16) # 16 is size here too
    }
    # so, as you can see we need to specify size of string in type, pls be careful or simply write 255 if you don't mind memory usage
    
    # 3. Add a player (key = 1)
    stark> add player 1 name="Hero" hp=100 level=5 gold=250 class="warrior"
    
    # 4. Add another player (key = 2)
    stark> add player 2 name="Mage" hp=80 level=7 gold=450 class="wizard"
    
    # 5. Get player data
    stark> get player 1
    # Output: name="Hero" hp=100 level=5 gold=250 class="warrior"
    
    stark> get player 2
    # Output: name="Mage" hp=80 level=7 gold=450 class="wizard"
    
    # 6. Check if player exists
    stark> get player 3
    # Output: player:3 not found
    
    # 7. View type definition
    stark> desc player
    # Output shows all fields with their types
    
    # 8. Update player (overwrites)
    stark> add player 1 name="Hero" hp=85 level=6 gold=300 class="warrior"
    
    # 9. Verify update
    stark> get player 1
    # Output: name="Hero" hp=85 level=6 gold=300 class="warrior"
    
    # 10. Check database stats
    stark> stats
    # Output shows total keys, data size, etc.
    
    # 11. Exit
    stark> exit

### Transaction commands
Transactions works like a surgeon, begin, does its job, and commit and done. It is used for updating values in database. The best use case of transactions is game development. 
begin -> **start** the transaction process
commit -> **end** the transaction process
rollback -> **rollback** the transaction ( if the transaction fails, use this)


Here is an example:

    stark> define player {id int hp int level int}
    stark> add player 1 id=123 hp=100 level=0
    # Say at this point, player won a fight against monster
    stark> begin #start transaction
    stark> add player 1 id=123 hp=45 level=1
    stark> commit #commit and save changes

# Using STARK in C++
This is the most important part, because databases don't just stay there as it is. People connect them with their code to write APIs. For now, STARK only supports C++, further updates will cover far more languages such as Vyne, Python, Java, Go. It also supports C because it is written in it.

    
    // Open database
    stark::Database db("filename"); 
    
    // Numeric keys
    db.add(1, "value");
    std::string v = db.get(1);
    db.remove(1);
    if (db.exists(1)) {}
    
    // String keys
    db.put_str("key", "value");
    std::string v = db.get_str("key");
    db.remove_str("key");
    if (db.exists_str("key")) {}
    
    // Typed data
    db.define_type("name", {{"field", TYPE_INT}});
    db.add_typed("name", 1, "field=100");
    std::string v = db.get_typed("name", 1);
    auto fields = db.describe_type("name");
    db.undefine_type("name");
    
    // Transactions
    db.begin();
    db.commit();
    db.rollback();
    if (db.in_transaction()) {}
    
    // Utilities
    db.sync();
    auto stats = db.stats();
    std::string err = db.get_last_error();


# Quick compariosn table
## In terms of core features

| Feature         | STARK      | SQLite      | Redis        | MongoDB     | LevelDB     |
|-----------------|-----------|------------|-------------|------------|------------|
| Type            | Key-Value | Relational | Key-Value   | Document   | Key-Value  |
| Language        | C         | C          | C           | C++        | C++        |
| Setup Time      | 5 minute  | 5 minutes  | 10 minutes  | 15 minutes | 5 minutes  |
| Memory Usage    | 2-5 MB    | 5-10 MB    | 5-10 MB     | 100+ MB    | 5-10 MB    |
| File Size       | < 1 MB    | 1-5 MB     | N/A (RAM)   | 200+ MB    | 1-5 MB     |
| Learning Curve  | 20 minutes| 1 hour     | 30 minutes  | 2 hours    | 30 minutes |
| Transactions    | ✅ Yes    | ✅ Yes     | ✅ Yes      | ✅ Yes     | ❌ No      |
| ACID            | ✅ Yes    | ✅ Yes     | ✅ Partial  | ✅ Yes     | ❌ No      |
| SQL Support     | ❌ No     | ✅ Yes     | ❌ No       | ❌ No      | ❌ No      |
| Network Support | ❌ No     | ❌ No      | ✅ Yes      | ✅ Yes     | ❌ No      |
| Multi-user      | ❌ No     | ✅ Yes     | ✅ Yes      | ✅ Yes     | ❌ No      |

## In terms of usage and development

| Feature          | STARK              | SQLite       | Redis       | MongoDB      |
|------------------|------------------|-------------|------------|-------------|
| Save Player Data | ✅ 1 line         | 5 lines SQL | 2 lines    | 3 lines JSON |
| Load in 1 sec    | ✅ 0.5 µs         | 3 µs        | 1 µs       | 50 µs       |
| Offline Use      | ✅ Yes            | ✅ Yes      | ❌ No      | ❌ No       |
| Simple Syntax    | ✅ add(1, "Hero") | INSERT INTO | SET key val | db.insert() |
| Focus       | ✅ Built mainly for games| General purpose | Cache    | Web apps    |

## In terms of size per 1000 records

| Database | File Size |
|----------|-----------|
| STARK    | ~100 KB   |
| SQLite   | ~150 KB   |
| MongoDB  | ~2 MB     |
| LevelDB  | ~120 KB   |

## In terms of speed

| Operation         | STARK       | SQLite      | Redis       | MongoDB     |
|------------------|------------|------------|------------|------------|
| Write 1 record    | 0.8 µs     | 5 µs       | 1 µs       | 50 µs      |
| Read 1 record     | 0.5 µs     | 3 µs       | 0.8 µs     | 30 µs      |
| Write 1000 records| 0.8 ms     | 5 ms       | 1 ms       | 50 ms      |
| Read 1000 records | 0.5 ms     | 3 ms       | 0.8 ms     | 30 ms      |


**As estimated values shows, STARK is faster and better than some of other local databases. Of course, it can't compete with them, becuase other databases have plenty of features that can be used for any purposes. What makes STARK better than them is its strict syntax and focus on core functions. In other words, STARK is designed for specific use cases, and this is what makes it better at those.**

# 👍 Why Choose STARK?

| Reason       | Explanation                     |
|-------------|---------------------------------|
| Simplest    | 1 line to save, 1 line to load |
| Smallest    | 2 MB memory, < 1 MB file size  |
| Fastest     | 0.5 µs reads — instant!        |
| Game Ready  | Built for game developers      |
| Free        | Open source, MIT license       |

# 👎 When NOT to Choose STARK

| Situation                         | Better Choice          |
|----------------------------------|----------------------|
| Need SQL queries                  | SQLite               |
| Need network access               | Redis, MongoDB       |
| Multi-user app                    | PostgreSQL           |
| Big data analytics                | MongoDB              |
| Mobile app with complex queries   | SQLite               |


If you have any questions, advices or want to report an issue, feel completely free to contact:
novruzluabdullah03@gmail.com

