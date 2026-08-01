# STARKDB — Complete Context & Reference Guide

**Version:** 1.1.0 (Python binding `starkdb` package: 1.1.0)
**License:** MIT
**Language:** Core is C99; bindings for C++, C, and Python (ctypes).
**Author / Contact:** Abdullah Novruzlu — novruzluabdullah03@gmail.com
**Repo / Website:** https://github.com/abdullahtnz/starkdb · https://starkdb.org

> This document is a self-contained context guide. An AI Agent or LLM reading **only**
> this file should be able to understand, build, and use every feature of STARKDB:
> the CLI, the C API, the C++ API, and the Python API (including the ML features).

---

## 1. What STARKDB Is

STARKDB (also called "STARK") is a **lightweight, embedded, key-value database**
written in C with a **B-tree index**. It is designed for:

- **Offline / local** applications (no network, no server)
- **Video games** (save systems, player/entity/inventory data)
- **Mobile and desktop applications**
- **ML workloads** (NumPy zero-copy arrays, Arrow-style column layout for DataFrames)

It is intentionally *simple*: no SQL, no network, no multi-user server. It trades those
features for speed, tiny footprint, and a minimal learning curve.

Positioning facts (as documented by the project):

| Attribute | Value |
|---|---|
| Type | Embedded key-value (B-tree indexed) |
| RAM usage | 2–5 MB |
| File size | < 1 MB (≈100 KB per 1000 records) |
| Read speed | ~0.5 µs per record |
| Write speed | ~0.8 µs per record |
| Setup time | ~5 minutes |
| ACID | Yes |
| SQL | No |
| Network | No |
| Multi-user | No |

---

## 2. Core Concept — "Workflow"

STARK persists data to **two files per database**. If you open a database named
`mydb`, two files are created / opened in the current working directory:

| File | Purpose |
|---|---|
| `mydb.idx` | **B-tree index** — stores keys and data addresses (pointers into `.dat`). Enables O(log n) lookups and sorted iteration. |
| `mydb.dat` | **Data file** — stores the actual value bytes, append-only. |

**Backup rule (important):** Always copy **both** files together. They must remain
in the same directory with the same base name. Deleting/moving one without the other
corrupts the database.

Data can be manipulated through three interfaces:
1. **CLI** (`stark_cli`)
2. **C++ API** (`#include <stark.hpp>`)
3. **Python API** (`import starkdb`)

Under the hood all three call the same **C core library** `libstark`.

---

## 3. Three Key Modes

STARK treats keys differently depending on their kind. This is a core design decision.

### 3.1 Numeric keys (`uint32_t`)
- Integer keys from `0` to `4,294,967,295`.
- Stored directly in the B-tree, sorted numerically.
- Ideal for IDs: player IDs, item IDs, save slots.
- Values are arbitrary raw bytes.

### 3.2 String keys
- Text keys (`std::string` / Python `str` / C `char*`).
- **Hashed internally with the djb2 algorithm** and stored as a `uint32_t` key:
  ```c
  hash = 5381;
  while ((c = *str++)) hash = ((hash << 5) + hash) + c;
  ```
- You always interact via the original text; the hash is internal.
- Because hashing maps strings into the same `uint32_t` space as numeric keys,
  a string key and a numeric key **can theoretically collide** (djb2 is not
  collision-free). String keys and numeric keys are stored in the same B-tree.

### 3.3 Typed records (user-defined "structs")
- Structured data with named fields, defined by the user.
- Stored internally under a **hashed string key** of the form `"type_name:key"`
  (e.g., `"player:1"`), so each typed record is one string-keyed blob.
- The type *definition* itself is stored under the string key `"type:name"`
  (e.g., `"type:player"`).
- Field types supported: `int` (TYPE_INT = 1, 4 bytes) and `string(N)`
  (TYPE_STRING = 2, N+1 bytes including null terminator).

---

## 4. Architecture & Storage Internals

### 4.1 Modules
| Module | File(s) | Role |
|---|---|---|
| Public C API | `core/include/stark.h` | The full C API surface (`stark_*` functions) |
| Type structs | `core/include/type.h` | `FieldDef`, `TypeDef`, `TYPE_INT`/`TYPE_STRING` |
| API implementation | `core/src/database_api.c` | Implements every `stark_*` function; owns handle struct |
| Database layer | `core/src/database.c` / `.h` | Combines B-tree + storage; CRUD, binary, columns |
| B-tree | `core/src/btree.c` / `.h` | Index tree + cursor iteration |
| Storage | `core/src/storage.c` / `.h` | Append-only block writer over the pager |
| Pager | `core/src/pager.c` / `.h` | Page cache over the `.idx`/`.dat` files |
| Type system | `core/src/type.c` | Type create/get/delete, serialize/deserialize fields |
| CLI | `cli/src/main.c` | Interactive REPL |
| C++ binding | `bindings/cpp/stark.hpp` | Header-only wrapper (`stark::Database`) |
| Python binding | `bindings/python/starkdb/` | `__init__.py`, `_lib.py` (ctypes), `database.py` |

### 4.2 Constants (`core/src/constants.h`)
```c
#define PAGE_SIZE 65536        // 64 KB pages — large enough for ML arrays
#define TABLE_MAX_PAGES 200    // max pages cached in memory
#define INVALID_PAGE UINT32_MAX

typedef enum { DB_SUCCESS=0, DB_ERROR=-1, DB_NOT_FOUND=-2,
               DB_FULL=-3, DB_IO_ERROR=-4, DB_MEMORY_ERROR=-5 } DB_Result;
```

### 4.3 Pager (`pager.c`)
- Opens file in `rb+`, or creates it `wb+` if it does not exist.
- `num_pages = file_size / PAGE_SIZE`; the file must be a multiple of `PAGE_SIZE`.
- Page cache is a fixed array of 200 pointers; pages are allocated lazily with `calloc`.
- `pager_allocate_page` scans for the first free cache slot; each new page is
  zero-initialized in memory and flushed on close / `stark_sync`.

### 4.4 B-tree (`btree.c`)
- Nodes: **leaf** and **internal**, stored one per 64 KB page.
- Leaf: `num_cells` (max **31**), `keys[31]`, `values[31]` (value = `data_addr_t`,
  i.e., address into the `.dat` file).
- Internal: `num_keys` (max **30**), `keys[30]`, `children[31]` (page numbers).
- Operations: `btree_insert`, `btree_find`, `btree_delete`, `btree_count_keys`,
  plus a full cursor (`btree_cursor_create/first/last/next/prev/get/destroy`).
- Duplicate keys: re-inserting an existing key writes the new value cell
  (leaf_node_insert inserts in sorted order; existing entries are effectively
  overwritten by insertion of the new value, and `db_insert` writes a new data
  block each time — old data blocks are left as garbage).
- `btree_count_keys` only counts the left-most leaf, so **it is inaccurate for
  multi-page trees** (see Caveats).

### 4.5 Storage / data file (`storage.c`)
- Page layout for a data page:
  - offset 0: `uint32 free_offset` (next free byte)
  - offset 4: `uint32 block_count`
  - offset 8 onward: blocks, each `uint32 data_size` followed by `data_size` bytes.
- `DATA_PAGE_START_RESERVE = 8`.
- `storage_write` appends a block to the current page; if the block does not fit,
  it allocates a new page and continues there. **A single block must fit within
  one page** (max value ≈ 65536 − 8 − 4 = ~65,524 bytes).
- Address format: `MAKE_ADDR(page, offset) = (page << 32) | offset`.
- `storage_delete` just zeroes the block's size header (does not reclaim space).

### 4.6 Type system (`type.c`)
- Type definitions are serialized as a `TypeDef` struct and stored under the
  string key `"type:<name>"` using `stark_put_str`.
- Field serialization: `type_serialize` parses `"field=value field=value"` tokens
  and writes values at each field's byte offset. `int` is written as `uint32_t`;
  `string` is copied (quotes stripped).
- Deserialization: `type_deserialize` prints `field=value` (ints plain,
  strings quoted) separated by spaces.
- `type_list` / `stark_list_types` is a **stub** — it only prints a message and
  does not return real type names (see Caveats).

### 4.7 Data layout recap for the `.dat` file
Values are stored as raw bytes. **The Python binding writes bytes without a
trailing NUL; the C++ binding writes `value.size() + 1` bytes (includes the NUL).**
This matters when data written by one binding is read by another.

---

## 5. Repository Layout

```
starkdb/
├── CMakeLists.txt                 # Builds libstark + stark_cli; installs headers
├── README.md                      # Main user-facing readme
├── CONTEXT.md                     # THIS FILE
├── LICENSE                        # MIT
├── core/
│   ├── include/  stark.h, type.h, type_funcs.h
│   └── src/      database_api.c, database.c/.h, btree.c/.h,
│                 storage.c/.h, pager.c/.h, constants.h, type.c
├── cli/src/main.c                 # stark_cli REPL
├── bindings/
│   ├── cpp/      stark.hpp, README.md
│   └── python/   starkdb/ (__init__.py, _lib.py, database.py),
│                 test_starkdb.py, setup.py, pyproject.toml, MANIFEST.in, README.md
├── examples/cpp/ test.cpp, CMakeLists.txt
├── docs/index.html                # Full website docs (CLI, C, C++, Python)
├── installation/index.html        # Website install guide
├── index.html, 404.html, js/, css/, assets/   # Marketing website
├── lab/                           # In-browser interactive STARK simulator (JS)
└── logs/index.html                # Changelog page
```

---

## 6. Build & Installation

### 6.1 Prerequisites
- CMake ≥ 3.10
- A C99 compiler (GCC/MinGW); `g++` for the C++ example
- `make` (Linux) or `mingw32-make` (Windows)
- Python ≥ 3.8 (for the Python binding)
- Optional for ML features: `numpy`, `pandas`

### 6.2 Linux
```bash
git clone https://github.com/abdullahtnz/starkdb.git
cd starkdb
mkdir build && cd build
cmake .. -DBUILD_SHARED=ON
make

# One-time system install (optional):
sudo make install      # copies libstark.so + stark.h to /usr/local
sudo ldconfig
```
- Build outputs in `build/`: `libstark.so`, `stark_cli`.
- The library file is named `libstark.so` (this exact name matters for Python).

### 6.3 Windows (MinGW)
```bat
cd C:\my_project\starkdb
mkdir build && cd build
cmake .. -G "MinGW Makefiles" -DBUILD_SHARED=ON
mingw32-make

:: One-time install (copy into MinGW so any project can use it)
copy libstark.dll C:\mingw64\bin\
copy libstark.dll.a C:\mingw64\lib\
copy ..\bindings\cpp\stark.hpp C:\mingw64\include\
copy ..\core\include\stark.h C:\mingw64\include\
```

### 6.4 C/C++ usage after install
```cpp
#include <stark.hpp>      // C++ binding (header-only)
// or
#include <stark.h>        // C API

// Compile:
//   g++ main.cpp -lstark -o app
//   gcc  main.c  -lstark -o app
```

### 6.5 Python binding installation
```bash
# 1. Build the C library first (see above) — produces build/libstark.so
# 2. Install the Python package (editable):
cd starkdb/bindings/python
pip install -e .
# 3. Optional ML dependencies:
pip install numpy pandas
```
The Python package is a **pure-ctypes FFI wrapper** — there is no C extension to
compile. At runtime it locates `libstark.so` / `libstark.dylib` / `stark.dll` by
searching, **in order**:

1. Relative to the package: `<pkg>/../../build`, `<pkg>/../../../../build`
   (i.e., the repo `build/` dir), and the current working directory.
2. System library paths (via `ctypes.util.find_library`).
3. Directories in `LD_LIBRARY_PATH`.

If the library is not found, you will get a `RuntimeError` telling you to build it,
or to set the **`STARK_LIB_PATH`** environment variable:

```bash
export STARK_LIB_PATH=/path/to/libstark.so
```

---

## 7. CLI Reference (`stark_cli`)

Open or create a database:
```bash
./stark_cli mygame          # creates/opens mygame.idx + mygame.dat in cwd
```
`stark_cli <filename>` — the filename has no extension; STARK appends `.idx`/`.dat`.
If omitted, it defaults to `mydb`. Inside the REPL (`stark>` prompt):

### General commands
| Command | Description |
|---|---|
| `help` | Show all commands |
| `stats` | Show keys count, b-tree height, data size, page count |
| `sync` | Flush pages to disk |
| `exit` | Flush + close + quit |

### Numeric-key commands
| Command | Description |
|---|---|
| `addn <key> <value>` | Insert/update numeric key (value may be quoted string or number) |
| `getn <key>` | Get value for numeric key |
| `deln <key>` | Delete numeric key |
| `existsn <key>` | Check if numeric key exists |

### String-key commands
| Command | Description |
|---|---|
| `adds <key> <value>` | Insert/update string key |
| `gets <key>` | Get value for string key |
| `dels <key>` | Delete string key |
| `exists_str <key>` | Check if string key exists |

### Type commands
| Command | Description |
|---|---|
| `define <name> { f1 t1 f2 t2 ... }` | Define a new type (braces optional). Fields: `name int` or `name string(N)`. |
| `undefine <name>` | Delete a type definition |
| `desc <name>` | Show a type's fields and offsets |

### Typed-data commands
| Command | Description |
|---|---|
| `add <type> <key> field=value ...` | Add/update a typed record |
| `get <type> <key>` | Get a typed record as `field=value ...` |

### Transaction commands
| Command | Description |
|---|---|
| `begin` | Start a transaction |
| `commit` | Commit the transaction |
| `rollback` | Roll back the transaction |

### CLI example (game save system)
```
stark_cli mygame
stark> define player { name string(32) hp int level int gold int class string(16) }
stark> add player 1 name="Hero" hp=100 level=5 gold=250 class="warrior"
stark> get player 1
# Output: name="Hero" hp=100 level=5 gold=250 class="warrior"
stark> get player 3
# Output: player:3 not found
stark> desc player
stark> begin
stark> add player 1 name="Hero" hp=85 level=6 gold=300 class="warrior"
stark> commit
stark> stats
stark> exit
```

> **CLI parsing notes:** `addn`/`getn` parse a single whitespace-delimited value
> token (`%255s`), so values containing spaces must be handled with care.
> `define` supports both `define name { ... }` and `define name f1 t1 ...`.

---

## 8. C API Reference (`core/include/stark.h`)

### 8.1 Handle & result codes
```c
typedef struct stark_db stark_db_t;

typedef enum {
    STARK_OK = 0,
    STARK_ERROR = -1,
    STARK_NOT_FOUND = -2,
    STARK_FULL = -3,
    STARK_IO_ERROR = -4,
    STARK_INVALID_ARG = -5,
    STARK_CLOSED = -6,
    STARK_MEMORY_ERROR = -7
} stark_result_t;
```
*(The older result-code table on the website docs page is outdated; the enum above
is the authoritative one.)*

### 8.2 dtypes (for binary arrays)
```c
typedef enum {
    STARK_DTYPE_FLOAT32 = 0, STARK_DTYPE_FLOAT64 = 1,
    STARK_DTYPE_INT32 = 2,   STARK_DTYPE_INT64 = 3,
    STARK_DTYPE_UINT8 = 4,   STARK_DTYPE_INT8 = 5,
    STARK_DTYPE_INT16 = 6
} stark_dtype_t;
```

### 8.3 Lifecycle
```c
stark_db_t* stark_open(const char* path, unsigned flags); // flags ignored (pass 0)
void stark_close(stark_db_t* db);                          // syncs, then closes
```

### 8.4 CRUD (numeric keys)
```c
stark_result_t stark_put(stark_db_t* db, uint32_t key, const void* value, size_t value_size);
stark_result_t stark_add(stark_db_t* db, uint32_t key, const void* value, size_t value_size);
//   put == add (both insert-or-update). value_size must be > 0.
stark_result_t stark_get(stark_db_t* db, uint32_t key, void* buffer, size_t* buffer_size);
//   *buffer_size is in/out: pass buffer capacity, returns actual size.
//   Returns STARK_ERROR if buffer too small (and sets *buffer_size to needed size).
stark_result_t stark_delete(stark_db_t* db, uint32_t key);
int stark_exists(stark_db_t* db, uint32_t key);   // 1 = exists, 0 = no
```

### 8.5 Cursor iteration (all keys, ascending order)
```c
stark_cursor_t* stark_cursor_create(stark_db_t* db);
stark_result_t stark_cursor_first(stark_cursor_t* c);   // to smallest key
stark_result_t stark_cursor_last(stark_cursor_t* c);    // to largest key
stark_result_t stark_cursor_next(stark_cursor_t* c);    // STARK_NOT_FOUND at end
stark_result_t stark_cursor_prev(stark_cursor_t* c);    // STARK_NOT_FOUND at start
stark_result_t stark_cursor_get(stark_cursor_t* c, uint32_t* key, void* buffer, size_t* buffer_size);
void stark_cursor_destroy(stark_cursor_t* c);
```

### 8.6 String keys
```c
stark_result_t stark_put_str(stark_db_t* db, const char* key, const void* value, size_t value_size);
stark_result_t stark_get_str(stark_db_t* db, const char* key, void* buffer, size_t* buffer_size);
//   If buffer == NULL: acts as a size probe — returns STARK_ERROR with *buffer_size
//   set if the key exists, STARK_NOT_FOUND otherwise. (The type system relies on this.)
stark_result_t stark_del_str(stark_db_t* db, const char* key);
int stark_exists_str(stark_db_t* db, const char* key);
```
String keys are djb2-hashed to `uint32_t` and share the B-tree with numeric keys.

### 8.7 Binary array storage (ML)
```c
stark_result_t stark_store_binary(stark_db_t* db, uint32_t key, stark_dtype_t dtype,
                                  const void* data, size_t elem_size, size_t count);
//   If elem_size == 0 it is derived from dtype.
stark_result_t stark_load_binary(stark_db_t* db, uint32_t key, stark_dtype_t* dtype,
                                 void* buffer, size_t* count, size_t* elem_size);
stark_result_t stark_get_binary_ptr(stark_db_t* db, uint32_t key, stark_dtype_t* dtype,
                                    void** ptr, size_t* count, size_t* elem_size);
//   Zero-copy: *ptr points into DB memory; valid only until next DB write/close.
```
Storage layout of an array: `[size_t elem_size][size_t count][raw data]`.
On load, dtype is **inferred from element size**: 4→FLOAT32, 8→FLOAT64, 2→INT16,
1→UINT8, else INT32. (This makes `int32` ambiguous with `float32` — see Caveats.)

### 8.8 Arrow column layout (ML DataFrames)
```c
typedef struct {
    char*  schema_json;      // JSON string describing each column + its dtype
    void** column_data;      // array of raw column buffers
    size_t* column_sizes;    // byte size of each column
    size_t num_columns;
    size_t num_rows;
} stark_column_data_t;

stark_result_t stark_store_columns(stark_db_t* db, const char* column_key,
                                   const char* schema_json, void** columns,
                                   size_t* column_sizes, size_t num_columns,
                                   size_t num_rows);
stark_result_t stark_load_columns(stark_db_t* db, const char* column_key,
                                  stark_column_data_t* out);   // malloc'd; free with stark_free_columns
void stark_free_columns(stark_column_data_t* data);
```
Internally: the schema header is stored under hashed key `<column_key>` (magic
`0x4152524F` = "ARRO"), and each column `i` under the hashed key
`"<column_key>__col<i>"`.

### 8.9 Batch operations
```c
typedef struct {
    uint32_t* keys;    // malloc'd array
    void**    values;  // malloc'd array of malloc'd buffers
    size_t*   sizes;
    size_t    count;
} stark_batch_t;
stark_result_t stark_put_batch(stark_db_t* db, stark_batch_t* batch);
void stark_free_batch(stark_batch_t* batch);
```
`stark_put_batch` simply calls `stark_add` for each entry (sequential, no true
atomicity at the C level; the Python `put_batch` wraps it in begin/commit).

### 8.10 Statistics & utilities
```c
typedef struct {
    uint64_t keys_count;     // NOTE: from btree_count_keys — see Caveats
    uint32_t btree_height;   // NOTE: hard-coded to 1
    uint64_t data_size;
    uint32_t page_count;
} stark_stats_t;
stark_result_t stark_stats(stark_db_t* db, stark_stats_t* stats);
const char* stark_error(stark_db_t* db);
stark_result_t stark_sync(stark_db_t* db);   // flush all index + data pages
```

### 8.11 Type system
```c
stark_result_t stark_define_type(stark_db_t* db, const char* name,
                                 FieldDef* fields, uint32_t field_count);
stark_result_t stark_undefine_type(stark_db_t* db, const char* name);
TypeDef* stark_get_type(stark_db_t* db, const char* name);   // malloc'd; caller frees
stark_result_t stark_list_types(stark_db_t* db, char*** names, uint32_t* count); // STUB
stark_result_t stark_add_typed(stark_db_t* db, const char* type_name,
                               uint32_t key, const char* field_values); // "f=v f=v"
stark_result_t stark_get_typed(stark_db_t* db, const char* type_name,
                               uint32_t key, char* output, size_t output_size);
```

### 8.12 Transactions
```c
stark_result_t stark_begin(stark_db_t* db);
stark_result_t stark_commit(stark_db_t* db);
stark_result_t stark_rollback(stark_db_t* db);
int stark_in_transaction(stark_db_t* db);
```
> **Important caveat:** In the current C implementation `begin`/`commit`/`rollback`
> only toggle an in-transaction flag and allocate/free a placeholder log buffer.
> Writes performed between `begin` and `commit` are applied immediately and
> `rollback` does **not** actually undo them. Treat transactions as "best-effort"
> atomic grouping in this version (see §18).

---

## 9. C++ API Reference (`bindings/cpp/stark.hpp`)

Header-only wrapper, `namespace stark`. After install:
```cpp
#include <stark.hpp>
// link: -lstark
```

### 9.1 Exceptions
| Class | Base | Meaning |
|---|---|---|
| `stark::Error` | `std::runtime_error` | Generic STARK error |
| `stark::NotFound` | `stark::Error` | Key/record not found |

### 9.2 Support structs
```cpp
struct stark::Stats {          // wraps stark_stats_t
    uint64_t keys;             // keys_count
    uint32_t height;           // btree_height
    uint64_t data_size;
    uint32_t pages;            // page_count
};

struct stark::Field {          // wraps FieldDef
    std::string name;
    int type;                  // TYPE_INT (1) or TYPE_STRING (2)
    int size;                  // string buffer size (default 4); ignored for int
    Field(const std::string& n, int t, int s = 4);
};
```

### 9.3 Lifecycle
```cpp
explicit stark::Database(const std::string& path); // throws stark::Error if open fails
~Database();                                       // auto sync() + close()
// Move-constructible and move-assignable; NOT copyable (deleted).
```

### 9.4 Numeric key methods
```cpp
void add(uint32_t key, const std::string& value);   // insert or update
std::string get(uint32_t key);                      // "" if not found
std::string get(uint32_t key, const std::string& default_value); // fallback
bool remove(uint32_t key);                          // true if deleted, false if absent
bool exists(uint32_t key);
```

### 9.5 String key methods
```cpp
void put_str(const std::string& key, const std::string& value);
std::string get_str(const std::string& key);        // "" if not found
bool remove_str(const std::string& key);
bool exists_str(const std::string& key);
```

### 9.6 Type system
```cpp
void define_type(const std::string& name, const std::vector<Field>& fields);
//   Throws stark::Error "Type already exists" if it exists.
//   ⚠ BUG: Field::to_c() also sets offset = 0 and type_create does not recompute
//   offsets, so multi-field typed records overlap at offset 0 (last field wins).
//   Use the CLI for multi-field types. See Caveat §18.18.
void add_typed(const std::string& type, uint32_t key, const std::string& fields);
//   fields is a string like "name=Hero hp=100 level=5"
std::string get_typed(const std::string& type, uint32_t key);  // "" if not found
std::vector<Field> describe_type(const std::string& name);      // throws NotFound
bool undefine_type(const std::string& name);
```

### 9.7 Transactions
```cpp
void begin(); void commit(); void rollback();
bool in_transaction();
```

### 9.8 Utilities
```cpp
void sync();
Stats stats();
std::string get_last_error();   // from stark_error(db)
```

### 9.9 C++ quick-start example
```cpp
#include <stark.hpp>
#include <iostream>

int main() {
    stark::Database db("save");                 // creates save.idx + save.dat

    // Numeric keys
    db.add(1, "Hero");
    db.add(2, "100 HP");
    std::cout << db.get(1) << std::endl;        // Hero
    std::cout << db.get(99, "missing") << std::endl;  // missing
    std::cout << db.exists(1) << " " << db.remove(1) << std::endl;

    // String keys
    db.put_str("player:1", "Aldric");
    std::cout << db.get_str("player:1") << std::endl;  // Aldric

    // Typed data
    db.define_type("player", {
        stark::Field("name",  TYPE_STRING, 32),
        stark::Field("hp",    TYPE_INT),
        stark::Field("level", TYPE_INT),
    });
    db.add_typed("player", 1, "name=Hero hp=100 level=5");
    std::cout << db.get_typed("player", 1) << std::endl;  // name="Hero" hp=100 level=5
    auto f = db.describe_type("player");        // vector<Field>

    // Transactions
    db.begin();
    db.add(2, "updated");
    db.commit();

    // Utilities
    stark::Stats s = db.stats();
    std::cout << s.keys << " " << s.data_size << std::endl;
    db.sync();
    return 0;
}
```

> **C++ buffer limits:** `get()`/`get_str()` use a 4096-byte buffer and
> `get_typed()` uses 8192 bytes. Values larger than these cannot be read back
> through the C++ wrapper (use the C or Python API for larger blobs).
> C++ writes **include the trailing NUL byte** (`value.size()+1`).

---

## 10. Python API Reference (`starkdb`) — DETAILED

This is the highest-priority binding. The Python package is:
```
starkdb/
├── __init__.py     # exports Database, StarkDBError, StarkDBNotFound; __version__ = "1.1.0"
├── _lib.py         # ctypes bindings to libstark + result-code constants
└── database.py     # Database class (all high-level logic)
```
Import it as:
```python
import starkdb
db = starkdb.Database("mydb")
```

### 10.1 Exceptions
| Exception | Meaning |
|---|---|
| `starkdb.StarkDBError` | Base / generic error |
| `starkdb.StarkDBNotFound` | Key not found |
| `starkdb.StarkDBFull` | Storage full |
| `starkdb.StarkDBIOError` | I/O error |
| `starkdb.StarkDBValueError` | Invalid argument |
| `starkdb.StarkDBClosed` | Operation on a closed DB |
| `starkdb.StarkDBMemoryError` | Memory allocation failure |

All are subclasses of `StarkDBError`.

### 10.2 Lifecycle
```python
db = starkdb.Database("mydb")      # opens or creates mydb.idx + mydb.dat
db.close()                         # flushes (sync) then closes
db.closed                          # bool property
db.error()                         # str — last C-level error message
repr(db)                           # Database(path='mydb', closed=False)

# Context manager — auto-closes on exit:
with starkdb.Database("mydb") as db:
    db[1] = b"auto-saved"
# db.close() called automatically

# __del__ also auto-closes if still open.
```
All operations raise `StarkDBClosed` if the DB has been closed.

### 10.3 Numeric keys
```python
db.put(key, value)                 # value: bytes | str | bytearray (str/bytearray converted)
db.get(key, default=None)          # -> bytes; raises StarkDBNotFound if missing and default is None;
                                   #    otherwise returns `default`
db.get_text(key, default=None)     # -> str (utf-8, trailing NULs stripped); same default logic
db.delete(key)                     # -> bool (True if existed)
db.exists(key)                     # -> bool
```
Dict-style access (numeric keys only):
```python
db[key] = value                    # == put(key, value)
value = db[key]                    # == get(key) -> bytes
del db[key]                        # raises StarkDBNotFound if missing
key in db                          # == exists(key)
```

### 10.4 String keys
```python
db.put_str(key, value)             # key: str, value: str|bytes (str encoded utf-8)
db.get_str(key, default=None)      # -> bytes
db.get_str_text(key, default=None) # -> str (utf-8, trailing NULs stripped)
db.delete_str(key)                 # -> bool
db.exists_str(key)                 # -> bool
```
Keys are djb2-hashed internally; you always use the plain text.

### 10.5 Iteration
```python
for key in db:                     # all numeric (hashed) keys, ascending B-tree order
    print(key, db[key])
for key, value in db.items():      # (key, value) pairs, ascending
    ...
keys   = db.keys()                 # list of all keys
values = db.values()               # list of all values (bytes)
count  = db.count()                # len(keys)
```
> Note: because string keys are stored as djb2 hashes, iteration yields the
> **hashed uint32 values**, not the original strings.

### 10.6 Batch operations
```python
db.put_batch({1: b"A", 2: b"B", 3: b"C"})   # begin() -> put each -> commit();
                                            # rolls back on any error (atomic)
results = db.get_batch([1, 2, 3, 999])      # -> {1: b'A', 2: b'B', 3: b'C'}
                                            # missing keys silently skipped
```

### 10.7 Type system
```python
# Define a type: fields is a list of dicts: {name, type, size}
db.define_type("player", [
    {"name": "id",    "type": 1, "size": 4},    # type 1 = int
    {"name": "hp",    "type": 1, "size": 4},
    {"name": "level", "type": 1, "size": 4},
    {"name": "name",  "type": 2, "size": 32},   # type 2 = string(N)
])
# type: 1=TYPE_INT (size 4), 2=TYPE_STRING (size = buffer length, e.g. 32)

# Add / get records
db.add_typed("player", 1, "id=100 hp=50 level=10 name=Hero")  # field=value string
result = db.get_typed("player", 1)              # -> "id=100 hp=50 level=10 name=\"Hero\""
# Raises StarkDBNotFound if the record does not exist.

# Remove the type definition
db.undefine_type("player")
```
> There is **no `describe_type`** in the Python binding (it exists in C++).
> Field names are the dict keys "name"/"type"/"size".
>
> **Field offsets are computed automatically:** the Python `define_type` lays
> fields out sequentially (offset = sum of prior field sizes), matching the CLI
> behavior, and forces `int` fields to size 4.
> Multi-field types round-trip correctly:
> `[{"a",int},{"b",int}]` + `"a=100 b=50"` returns `a=100 b=50`.
> (The **C++** binding still passes `offset = 0` — see Caveat §18.18.)

### 10.8 Transactions
```python
db.begin()            # raises StarkDBError if already in a transaction
db.put(1, b"x"); db.put_str("k", "v")
db.commit()           # raises StarkDBError if not in a transaction
db.rollback()         # raises StarkDBError if not in a transaction
db.in_transaction()   # -> bool
```
> See §18 caveat — the C core currently does not truly undo writes on rollback.

### 10.9 Utilities & stats
```python
db.sync()                       # flush all dirty pages to disk
stats = db.stats()              # dict with keys:
#   "keys_count"  (int)  — NOTE: see Caveats on btree_count_keys
#   "btree_height" (int) — NOTE: hard-coded to 1
#   "data_size"   (int)
#   "page_count"  (int)
```

### 10.10 ML: Binary array storage (NumPy)
```python
import numpy as np

db.store_array(key, array, dtype=0)
#   array: numpy ndarray (preferred), OR (if numpy is unavailable):
#          bytes  -> stored as uint8
#          list/tuple of ints -> packed as int32 (via struct.pack)
#   dtype is auto-detected from arr.dtype for ndarrays:
#     float32->0, float64->1, int32->2, int64->3, uint8->4, int8->5, int16->6
#   The array is converted to a contiguous byte buffer (np.ascontiguousarray).

db.load_array(key)               # -> numpy array (SAFE COPY — independent buffer)
db.load_array_zerocopy(key)      # -> numpy array sharing DB memory (NO copy)
                                 # WARNING: pointer invalid after any DB write or close
```
Dtype mapping on load (from C's element-size inference):
`0→float32, 1→float64, 2→int32, 3→int64, 4→uint8, 5→int8, 6→int16`.

> **Caveat (important):** the C layer infers dtype from element byte size
> (4 bytes ⇒ float32, 8 ⇒ float64, 2 ⇒ int16, 1 ⇒ uint8). Therefore an array
> stored as `int32` (4 bytes) loads back as `float32` — the byte pattern is
> preserved but the dtype label is wrong. Use `float64` (8), `int16` (2), or
> `uint8`/`int8` (1) element types for unambiguous round-trips, or cast after load.

```python
# Full ML example
import numpy as np
import starkdb

db = starkdb.Database("ml_data")

# Store float32 embeddings
emb = np.array([0.1, 0.5, -0.3, 0.8], dtype=np.float32)
db.store_array(1, emb)
loaded = db.load_array(1)            # safe copy
print(loaded)                        # [0.1 0.5 -0.3 0.8]

# Zero-copy read
fast = db.load_array_zerocopy(1)     # shared memory; invalid after writes!
safe = fast.copy()                   # copy when you need to keep it

# Batch store 1000 x 768-dim vectors
for i in range(1000):
    db.store_array(1000 + i, np.random.randn(768).astype(np.float32))
db.close()
```

### 10.11 ML: Arrow column layout / DataFrame
```python
import pandas as pd
import numpy as np
import starkdb

db = starkdb.Database("datasets")

df = pd.DataFrame({
    "name": ["Alice", "Bob", "Charlie", "Diana"],
    "age":  np.array([25, 30, 35, 28], dtype=np.int32),
    "score": np.array([95.5, 87.2, 91.8, 78.9], dtype=np.float32),
})

db.store_dataframe("users", df)      # stores schema JSON + each column as a binary block
loaded = db.load_dataframe("users")  # -> pandas DataFrame
print(loaded.head())
db.close()
```
Implementation details an AI should know:
- `store_dataframe` accepts anything with a `.to_dict(orient='list')` (e.g., pandas
  DataFrame) or a plain dict of columns.
- **Columns are stored alphabetically sorted by name** (`sorted(data.keys())`),
  so the reloaded DataFrame may have a different column order than the original.
- String columns are joined with `\0` separators; numeric columns via `numpy`.
- Schema is a JSON string like `{"age": "int32", "name": "str", "score": "float32"}`.
- `load_dataframe` needs `pandas`, `numpy`, and `json` (stdlib). If pandas import
  fails at the end, it returns a plain dict of column arrays instead.
- This is **not** standard Arrow IPC file format — it is a columnar binary layout
  with a JSON schema header, designed for fast DataFrame reconstruction.

### 10.12 Python full-feature example (covers everything)
```python
import numpy as np
import pandas as pd
import starkdb
from starkdb import StarkDBNotFound, StarkDBError

# --- Lifecycle ---
with starkdb.Database("demo") as db:
    # --- Numeric keys ---
    db.put(1, b"Hello World")
    db[2] = "hero".encode()
    print(db[1])                        # b'Hello World'
    print(db.get_text(2))               # hero
    print(db.get(99, b"default"))       # b'default'
    assert 1 in db
    del db[1]
    assert db.delete(999) is False      # did not exist

    # --- String keys ---
    db.put_str("username", "player_one")
    print(db.get_str("username"))       # b'player_one'
    print(db.get_str_text("username"))  # player_one
    db.put_str("volume", "85")
    print(db.exists_str("volume"))      # True
    db.delete_str("volume")

    # --- Batch ---
    db.put_batch({10: b"A", 11: b"B", 12: b"C"})
    print(db.get_batch([10, 11, 12, 999]))  # {10: b'A', 11: b'B', 12: b'C'}

    # --- Iteration ---
    for key in db:
        print("key:", key)
    for key, value in db.items():
        print("item:", key, value)
    print(db.keys(), db.count())

    # --- Type system ---
    db.define_type("player", [
        {"name": "id",    "type": 1, "size": 4},
        {"name": "hp",    "type": 1, "size": 4},
        {"name": "level", "type": 1, "size": 4},
    ])
    db.add_typed("player", 1, "id=100 hp=50 level=10")
    print(db.get_typed("player", 1))    # id=100 hp=50 level=10
    db.undefine_type("player")

    # --- Transactions ---
    db.begin()
    db.put(50, b"txn value")
    db.commit()
    print(db.in_transaction())          # False

    # --- ML arrays ---
    arr = np.array([0.1, 0.5, -0.3], dtype=np.float32)
    db.store_array(900, arr)
    print(db.load_array(900))           # [ 0.1  0.5 -0.3]

    # --- DataFrame ---
    df = pd.DataFrame({"name": ["A", "B"], "age": np.array([1, 2], dtype=np.int32)})
    db.store_dataframe("users", df)
    print(db.load_dataframe("users"))

    # --- Stats ---
    print(db.stats())

# db is now closed automatically
try:
    db.get(1)
except StarkDBClosed:      # (subclass of StarkDBError)
    print("database is closed")
```
> **Note:** string keys are hashed, so a `for key in db` loop over a database that
> contains string keys yields the hashed integer keys, not the strings. Values
> written by C++ (with a NUL terminator) will read back with a trailing `\x00` in
> Python bytes; `get_text`/`get_str_text` strip it.

---

## 11. The Type System (shared across CLI / C / C++ / Python)

- **Define:** a type has a name and a list of `FieldDef` (`name`, `offset`, `type`,
  `size`).
  - `int` → TYPE_INT (1), size 4 bytes.
  - `string(N)` → TYPE_STRING (2), size N+1 bytes (N = max chars incl. NUL).
    Default N = 64 if unspecified (type.c) / 64 for the CLI parser.
- Fields are laid out sequentially in memory at their offsets; the record size is
  the sum of all field sizes.
- Type definitions are stored under the string key `type:<name>`.
- Records are stored under the string key `<type>:<key>` (hashed) as one blob.
- **Add = overwrite:** re-adding the same key replaces the whole record. There is
  no single-field update — you must provide the full `field=value ...` string.
- **Undefine** deletes the type *definition* only; per the code, records of that
  type are *not* cleaned up (see Caveats).
- String values may be quoted (`name="Hero"`) or unquoted in `add`/`add_typed`;
  quotes are stripped on serialize.
- **⚠ C++ binding offset bug:** only the CLI and the Python binding compute field
  offsets correctly. Types defined through the C++ binding (`Field::to_c()`)
  still put every field at offset 0 (see Caveat §18.18). Types defined via the
  CLI are stored in the DB and are usable from any binding.

---

## 12. Transactions (shared)

API exists in all interfaces:
```
C:      stark_begin / stark_commit / stark_rollback / stark_in_transaction
C++:    db.begin() / db.commit() / db.rollback() / db.in_transaction()
Python: db.begin() / db.commit() / db.rollback() / db.in_transaction()
CLI:    begin / commit / rollback
```
Intended semantics: group multiple writes into one atomic unit; `commit` persists
them, `rollback` discards them. **See §18 for the current implementation caveat.**

---

## 13. Cursor & Iteration (shared)

- The B-tree cursor iterates **all** keys in **ascending** order.
- Because all key kinds share one B-tree (numeric keys directly, string keys via
  djb2 hashes), iteration mixes them; string keys appear as their hash values.
- C API exposes the cursor directly; the C++ wrapper does **not** (use the C API);
  the Python wrapper exposes `iter(db)`, `items()`, `keys()`, `values()`, `count()`.

---

## 14. Error Handling Summary

### C
Every `stark_*` function returns a `stark_result_t` code (0 = OK, negative = error).
`stark_error(db)` returns the last human-readable error string.
Notable behaviors:
- `stark_get`: returns `STARK_ERROR` when the caller's buffer is too small and sets
  `*buffer_size` to the required size.
- `stark_get_str(db, key, NULL, &size)`: size-probe — `STARK_ERROR` (with size set)
  if found, `STARK_NOT_FOUND` if missing.
- `stark_put`/`stark_add` return `STARK_INVALID_ARG` for empty values
  (`value_size == 0`).

### C++
Exceptions: `stark::Error` (generic), `stark::NotFound` (not found).
- `get`/`get_str`/`get_typed` return `""` when a key is missing (they do **not**
  throw `NotFound`).
- `remove`/`remove_str`/`undefine_type` return `bool`.
- `describe_type` throws `NotFound` if the type is missing.
- Constructor throws `stark::Error` if the DB cannot be opened.

### Python
Exceptions listed in §10.1.
- `get`/`get_text`/`get_str`/`get_str_text` raise `StarkDBNotFound` unless
  `default` is supplied.
- `get_batch` silently skips missing keys.
- `get_typed` raises `StarkDBNotFound` for a missing record.
- Operations on a closed DB raise `StarkDBClosed`.

---

## 15. Performance & Size Characteristics

| Operation | STARK | SQLite | Redis | MongoDB |
|---|---|---|---|---|
| Write 1 record | 0.8 µs | 5 µs | 1 µs | 50 µs |
| Read 1 record | 0.5 µs | 3 µs | 0.8 µs | 30 µs |
| Write 1000 records | 0.8 ms | 5 ms | 1 ms | 50 ms |
| Read 1000 records | 0.5 ms | 3 ms | 0.8 ms | 30 ms |
| File size per 1000 records | ~100 KB | ~150 KB | — | ~2 MB |

These are the project's documented estimates for local, single-process use.

---

## 16. When to Use / When Not to Use

**Use STARKDB when:** you need an offline, embedded KV store for games, mobile, or
desktop apps; you want sub-microsecond reads; you want a tiny footprint; you want
NumPy/DataFrame integration in Python; you want a 5-minute setup.

**Do NOT use STARKDB when:** you need SQL / complex queries (use SQLite), network
access (Redis/MongoDB), multi-user concurrency (PostgreSQL), or big-data analytics
(MongoDB).

---

## 17. Testing

The Python bindings ship a self-contained test suite:
```bash
cd bindings/python
python3 test_starkdb.py
```
- It sets `STARK_LIB_PATH` to `../../build/libstark.so` automatically.
- Tests cover: basic CRUD, dict-style access, string keys, iteration/ordering,
  binary arrays (float32/int32/large), zero-copy, DataFrames, batch ops,
  transactions, context manager, stats, and the type system.
- Tests run under `/tmp/starkdb_test` and clean up afterward.
- `numpy`/`pandas` are optional; related tests are skipped if missing.

---

## 18. Known Implementation Notes & Caveats (IMPORTANT for accuracy)

An AI working on this codebase should know these — some documented behavior differs
from what the marketing docs claim:

1. **Transactions are nominal.** `stark_begin`/`stark_commit`/`stark_rollback`
   (database_api.c:504-546) only set/reset a flag and allocate/free a throwaway log
   buffer. Writes are applied immediately; **`rollback` does not restore old
   values.** The ACID/atomicity claims in the README/website describe the *intended*
   design, not the current implementation.

2. **`stats().keys_count` can be wrong.** `stark_stats` uses `btree_count_keys`,
   which only reads the leftmost leaf (btree.c:298-317). For small DBs (single
   leaf) it is accurate; for multi-page trees it undercounts.

3. **`stats().btree_height` is hard-coded to `1`** in `stark_stats`
   (database_api.c:384).

4. **`stark_list_types` / `type_list` is a stub** (type.c:157-176): it prints an
   info message and returns no names.

5. **`undefine_type` does not delete the type's records** (type.c:142-153) — only
   the definition under `type:<name>` is removed.

6. **Dtype ambiguity for binary arrays:** the C layer infers dtype from element
   byte size on load, so `int32` (4 bytes) loads back as `float32`. Prefer
   `float64`/`int16`/`uint8`/`int8` for exact dtype round-trips.

7. **String-key hashing collisions:** string keys are djb2-hashed into the same
   `uint32_t` space as numeric keys. Two different strings (or a string and a
   numeric key) can hash to the same key and overwrite each other.

8. **Garbage data blocks:** overwriting/deleting a key writes a new block to the
   `.dat` file but never reclaims the old one (append-only, `storage_delete` just
   zeroes a size header). Files grow with updates; a compact/defrag feature does
   not exist.

9. **Max value size:** a single value must fit inside one 64 KB page
   (≈65,524 bytes of payload). The Python `get`/`get_str` use 65536-byte buffers;
   larger stored values cannot be read back through those wrappers.

10. **C++ buffer limits:** `get`/`get_str` use 4096-byte buffers; `get_typed`
    uses 8192. Larger blobs require the C or Python API.

11. **NUL-termination mismatch:** C++ writes `value.size()+1` bytes (trailing NUL);
    Python writes exactly `len(value)` bytes. Cross-reading produces an extra
    `\x00` (stripped by `get_text`/`get_str_text`).

12. **`stark_open` ignores `flags`** (database_api.c:21-22).

13. **`examples/cpp/CMakeLists.txt` references `game_example.cpp`** which does not
    exist (only `test.cpp` exists); the `BUILD_CPP_EXAMPLES` option is OFF by
    default so the default build is unaffected.

14. **`type_get` prints debug messages** (`✅`/`❌`) to stdout (type.c:119-123) —
    the CLI and any C caller will see them.

15. **`_check_result` in `_lib.py` references exception classes not defined in that
    module** — it is unused dead code (high-level methods check codes directly),
    so it never executes.

16. **The website docs page lists an outdated C result-code table**
    (STARK_ERR=1, STARK_NOT_FOUND=2, ...). The authoritative values are the
    `stark_result_t` enum in `stark.h` (§8.1).

17. **C extension absence:** the Python binding is pure `ctypes`; no compilation
    of Python C extensions is needed, but `libstark.so` must exist (build first).

18. **Multi-field types are broken in the C++ binding (verified).**
    `Database::define_type` (C++, `Field::to_c()`) passes `FieldDef.offset = 0`
    for every field, and `type_create` (type.c:63-103) does not recompute
    offsets — it only sums sizes and stores the fields verbatim. Only the CLI
    path (via `type_parse_fields`) and the Python binding (since the fix in
    `database.py:define_type`, which computes sequential offsets and forces
    `int` fields to size 4) lay out fields correctly. Consequence in C++: all
    fields of a type live at byte offset 0, serialize on top of each other, and
    every field reads back the value of whichever field appears **last** in the
    `field_values` string. E.g. `[{"a",int},{"b",int}]` + `"a=100 b=50"` →
    `get_typed` returns `a=50 b=50`. **Workarounds for C++:** define multi-field
    types through the CLI or the Python binding (they are stored in the DB and
    usable from any binding), or use single-field types. Single-field types
    round-trip correctly.

---

## 19. Quick API Cheat Sheet

### Numeric keys
| C | C++ | Python |
|---|---|---|
| `stark_add(db,k,v,sz)` | `db.add(k, s)` | `db.put(k, b)` / `db[k]=b` |
| `stark_get(db,k,buf,&sz)` | `db.get(k)` | `db.get(k)` / `db[k]` |
| `stark_delete(db,k)` | `db.remove(k)` | `db.delete(k)` |
| `stark_exists(db,k)` | `db.exists(k)` | `db.exists(k)` / `k in db` |

### String keys
| C | C++ | Python |
|---|---|---|
| `stark_put_str(db,k,v,sz)` | `db.put_str(k,s)` | `db.put_str(k,s)` |
| `stark_get_str(db,k,buf,&sz)` | `db.get_str(k)` | `db.get_str(k)` / `db.get_str_text(k)` |
| `stark_del_str(db,k)` | `db.remove_str(k)` | `db.delete_str(k)` |
| `stark_exists_str(db,k)` | `db.exists_str(k)` | `db.exists_str(k)` |

### Types
| C | C++ | Python |
|---|---|---|
| `stark_define_type` | `db.define_type(name, {Field(...)})` | `db.define_type(name, [dict...])` |
| `stark_add_typed` | `db.add_typed(t,k,"f=v")` | `db.add_typed(t,k,"f=v")` |
| `stark_get_typed` | `db.get_typed(t,k)` | `db.get_typed(t,k)` |
| `stark_get_type` | `db.describe_type(name)` | — (not exposed) |
| `stark_undefine_type` | `db.undefine_type(name)` | `db.undefine_type(name)` |

### Transactions
| C | C++ | Python |
|---|---|---|
| `stark_begin/commit/rollback` | `db.begin()/commit()/rollback()` | `db.begin()/commit()/rollback()` |
| `stark_in_transaction` | `db.in_transaction()` | `db.in_transaction()` |

### Utilities
| C | C++ | Python |
|---|---|---|
| `stark_sync(db)` | `db.sync()` | `db.sync()` |
| `stark_stats(db,&s)` | `db.stats()` | `db.stats()` |
| `stark_error(db)` | `db.get_last_error()` | `db.error()` |
| cursor funcs | — | `iter(db)` / `db.items()` |

### ML
| C | Python |
|---|---|
| `stark_store_binary` | `db.store_array(k, np_arr)` |
| `stark_get_binary_ptr` | `db.load_array(k)` / `db.load_array_zerocopy(k)` |
| `stark_store_columns` | `db.store_dataframe(k, df)` |
| `stark_load_columns` | `db.load_dataframe(k)` |

---

## 20. Changelog highlights (from repo history & docs)

- v1.1.0 — Python bindings released (`starkdb` package); ML features
  (NumPy binary arrays, zero-copy, Arrow column layout, DataFrame support);
  batch operations; type system in Python.
- Earlier — B-tree index, CLI, C API, C++ bindings, transactions, types.

Contact for questions / bugs: novruzluabdullah03@gmail.com
