#ifndef STARK_CPP_HPP
#define STARK_CPP_HPP

#include <string>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <exception>

extern "C" {
    #include <stark.h>
}

namespace stark {

// ==================== Exception Classes ====================
class Error : public std::runtime_error {
public:
    explicit Error(const std::string& msg) : std::runtime_error(msg) {}
};

class NotFound : public Error {
public:
    NotFound() : Error("Key not found") {}
    explicit NotFound(const std::string& key) : Error("Key not found: " + key) {}
};

// ==================== Statistics Structure ====================
struct Stats {
    uint64_t keys;
    uint32_t height;
    uint64_t data_size;
    uint32_t pages;
    
    Stats() : keys(0), height(0), data_size(0), pages(0) {}
    
    explicit Stats(const stark_stats_t& s) 
        : keys(s.keys_count), height(s.btree_height), 
          data_size(s.data_size), pages(s.page_count) {}
};

// ==================== Field Definition ====================
struct Field {
    std::string name;
    int type;
    int size;
    
    Field(const std::string& n, int t, int s = 4) 
        : name(n), type(t), size(s) {}
    
    FieldDef to_c() const {
        FieldDef fd;
        std::strncpy(fd.name, name.c_str(), sizeof(fd.name) - 1);
        fd.name[sizeof(fd.name) - 1] = '\0';
        fd.type = type;
        fd.size = size;
        fd.offset = 0;
        return fd;
    }
};

// ==================== Main Database Class ====================
class Database {
private:
    stark_db_t* db;
    std::string last_error_msg;

    void check_db() const {
        if (!db) throw Error("Database not open");
    }

public:
    // ========== Constructor / Destructor ==========
    
    explicit Database(const std::string& path, uint32_t flags = 0) {
        stark_db_t* handle = nullptr;
        stark_open_error_t err = STARK_OPEN_ERR_INVALID_ARG;
        if (stark_open_ex(&handle, path.c_str(), flags, &err) != STARK_OK || !handle) {
            const char* msg = stark_last_open_error();
            throw Error(msg ? msg : ("Failed to open database: " + path));
        }
        db = handle;
    }
    
    ~Database() {
        if (db) {
            try {
                sync();
            } catch (...) {}
            stark_close(db);
            db = nullptr;
        }
    }
    
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    
    Database(Database&& other) noexcept : db(other.db) {
        other.db = nullptr;
    }
    
    Database& operator=(Database&& other) noexcept {
        if (this != &other) {
            if (db) stark_close(db);
            db = other.db;
            other.db = nullptr;
        }
        return *this;
    }
    
    // ========== Numeric Key Operations ==========
    // Uses: stark_add, stark_get, stark_delete, stark_exists
    
    void add(uint32_t key, const std::string& value) {
        check_db();
        stark_result_t r = stark_add(db, key, value.c_str(), value.size() + 1);
        if (r != STARK_OK) {
            throw Error("Failed to add key " + std::to_string(key));
        }
    }
    
    std::string get(uint32_t key) {
        check_db();
        size_t size = 0;
        stark_result_t r = stark_get(db, key, nullptr, &size);
        if (r == STARK_NOT_FOUND) return "";
        if (r != STARK_BUFFER_TOO_SMALL) {
            throw Error("Failed to get key " + std::to_string(key));
        }

        for (int attempt = 0; attempt < 4; attempt++) {
            std::vector<char> buffer(size > 0 ? size : 1);
            size_t capacity = buffer.size();
            r = stark_get(db, key, buffer.data(), &capacity);
            if (r == STARK_OK) return std::string(buffer.data(), capacity);
            if (r == STARK_BUFFER_TOO_SMALL) { size = capacity; continue; }
            if (r == STARK_NOT_FOUND) return "";
            throw Error("Failed to get key " + std::to_string(key));
        }
        throw Error("Value changed while reading key " + std::to_string(key));
    }
    
    std::string get(uint32_t key, const std::string& default_value) {
        std::string result = get(key);
        return result.empty() ? default_value : result;
    }
    
    bool remove(uint32_t key) {
        check_db();
        stark_result_t r = stark_delete(db, key);
        if (r == STARK_OK) return true;
        if (r == STARK_NOT_FOUND) return false;
        throw Error("Failed to remove key " + std::to_string(key));
    }
    
    bool exists(uint32_t key) {
        check_db();
        return stark_exists(db, key) != 0;
    }
    
    // ========== String Key Operations ==========
    // Uses: stark_put_str, stark_get_str, stark_del_str, stark_exists_str
    
    void put_str(const std::string& key, const std::string& value) {
        check_db();
        stark_result_t r = stark_put_str(db, key.c_str(), value.c_str(), value.size() + 1);
        if (r != STARK_OK) {
            throw Error("Failed to put string key: " + key);
        }
    }
    
    std::string get_str(const std::string& key) {
        check_db();
        size_t size = 0;
        stark_result_t r = stark_get_str(db, key.c_str(), nullptr, &size);
        if (r == STARK_NOT_FOUND) return "";
        if (r != STARK_BUFFER_TOO_SMALL) {
            throw Error("Failed to get string key: " + key);
        }

        for (int attempt = 0; attempt < 4; attempt++) {
            std::vector<char> buffer(size > 0 ? size : 1);
            size_t capacity = buffer.size();
            r = stark_get_str(db, key.c_str(), buffer.data(), &capacity);
            if (r == STARK_OK) return std::string(buffer.data(), capacity);
            if (r == STARK_BUFFER_TOO_SMALL) { size = capacity; continue; }
            if (r == STARK_NOT_FOUND) return "";
            throw Error("Failed to get string key: " + key);
        }
        throw Error("Value changed while reading string key: " + key);
    }
    
    bool remove_str(const std::string& key) {
        check_db();
        stark_result_t r = stark_del_str(db, key.c_str());
        if (r == STARK_OK) return true;
        if (r == STARK_NOT_FOUND) return false;
        throw Error("Failed to delete string key: " + key);
    }
    
    bool exists_str(const std::string& key) {
        check_db();
        return stark_exists_str(db, key.c_str()) != 0;
    }
    
    // ========== Type System ==========
    // Uses: stark_define_type, stark_undefine_type, stark_get_type,
    //       stark_add_typed, stark_get_typed
    
    void define_type(const std::string& name, const std::vector<Field>& fields) {
        check_db();

        if (fields.empty()) {
            throw Error("type must have at least one field");
        }

        std::vector<FieldDef> c_fields;
        c_fields.reserve(fields.size());
        uint32_t offset = 0;

        for (const auto& f : fields) {
            if (f.type == TYPE_STRING && f.size < 1) {
                throw Error("string field size must be >= 1");
            }
            for (const auto& existing : c_fields) {
                if (f.name == existing.name) {
                    throw Error("duplicate field name: " + f.name);
                }
            }

            FieldDef def = f.to_c();
            def.offset = offset;
            if (f.type == TYPE_INT) {
                def.size = 4;  // ints are always 4 bytes (matches CLI/Python)
            }
            offset += def.size;
            c_fields.push_back(def);
        }

        stark_result_t r = stark_define_type(db, name.c_str(), 
                                             c_fields.data(), c_fields.size());
        if (r != STARK_OK) {
            if (r == STARK_ERROR) {
                throw Error("Type already exists: " + name);
            } else {
                throw Error("Failed to define type: " + name);
            }
        }
    }
    
    void add_typed(const std::string& type, uint32_t key, const std::string& fields) {
        check_db();
        stark_result_t r = stark_add_typed(db, type.c_str(), key, fields.c_str());
        if (r == STARK_NOT_FOUND) {
            throw NotFound("Type not found: " + type);
        } else if (r != STARK_OK) {
            throw Error("Failed to add typed data");
        }
    }
    
    std::string get_typed(const std::string& type, uint32_t key) {
        check_db();
        size_t capacity = 8192;
        for (int attempt = 0; attempt < 8; attempt++) {
            std::vector<char> buffer(capacity + 1);
            stark_result_t r = stark_get_typed(
                db, type.c_str(), key, buffer.data(), capacity
            );
            if (r == STARK_OK) return std::string(buffer.data());
            if (r == STARK_NOT_FOUND) return "";
            if (r != STARK_BUFFER_TOO_SMALL) {
                throw Error("Failed to get typed data");
            }

            size_t required = 0;
            std::string err = get_last_error();
            if (std::sscanf(err.c_str(), "buffer too small: required %zu bytes",
                            &required) == 1 && required > capacity) {
                capacity = required;
            } else {
                capacity *= 2;
            }
        }
        throw Error("Typed value changed while reading");
    }
    
    std::vector<Field> describe_type(const std::string& name) {
        check_db();
        TypeDef* type = stark_get_type(db, name.c_str());
        if (!type) {
            throw NotFound("Type not found: " + name);
        }
        
        std::vector<Field> fields;
        for (uint32_t i = 0; i < type->field_count; i++) {
            fields.emplace_back(
                type->fields[i].name,
                type->fields[i].type,
                type->fields[i].size
            );
        }
        
        free(type);
        return fields;
    }
    
    bool undefine_type(const std::string& name) {
        check_db();
        stark_result_t r = stark_undefine_type(db, name.c_str());
        if (r == STARK_OK) return true;
        if (r == STARK_NOT_FOUND) return false;
        throw Error("Failed to undefine type: " + name);
    }
    
    // ========== Transactions ==========
    // Uses: stark_begin, stark_commit, stark_rollback, stark_in_transaction
    
    void begin() {
        check_db();
        stark_result_t r = stark_begin(db);
        if (r != STARK_OK) {
            throw Error("Failed to begin transaction");
        }
    }
    
    void commit() {
        check_db();
        stark_result_t r = stark_commit(db);
        if (r != STARK_OK) {
            throw Error("Failed to commit transaction");
        }
    }
    
    void rollback() {
        check_db();
        stark_result_t r = stark_rollback(db);
        if (r != STARK_OK) {
            throw Error("Failed to rollback transaction");
        }
    }
    
    bool in_transaction() {
        check_db();
        return stark_in_transaction(db) != 0;
    }
    
    // ========== Utilities ==========
    // Uses: stark_sync, stark_stats, stark_error
    
    void sync() {
        check_db();
        stark_result_t r = stark_sync(db);
        if (r != STARK_OK) {
            throw Error("Failed to sync database");
        }
    }

    void compact() {
        check_db();
        stark_result_t r = stark_compact(db);
        if (r != STARK_OK) {
            throw Error("Failed to compact database");
        }
    }

    void compact_stats(uint64_t& before_bytes, uint64_t& after_bytes) {
        check_db();
        stark_result_t r = stark_compact_stats(db, &before_bytes, &after_bytes);
        if (r != STARK_OK) {
            throw Error("Failed to compact database");
        }
    }

    void backup(const std::string& dest_prefix) {
        check_db();
        stark_result_t r = stark_backup(db, dest_prefix.c_str());
        if (r != STARK_OK) {
            throw Error(get_last_error());
        }
    }

    template<typename Fn>
    void foreach_range(uint32_t key_min, uint32_t key_max, Fn fn) {
        check_db();
        struct Context {
            Fn* fn;
            std::exception_ptr error;
        } context{&fn, nullptr};

        auto callback = [](uint32_t key, const void* value, size_t size,
                           void* user) -> int {
            Context* ctx = static_cast<Context*>(user);
            try {
                (*ctx->fn)(key, value, size);
                return 0;
            } catch (...) {
                ctx->error = std::current_exception();
                return 1;
            }
        };

        stark_result_t r = stark_foreach_range(
            db, key_min, key_max, callback, &context
        );
        if (context.error) std::rethrow_exception(context.error);
        if (r != STARK_OK) {
            throw Error(get_last_error());
        }
    }
    
    Stats stats() {
        check_db();
        stark_stats_t c_stats;
        stark_result_t r = stark_stats(db, &c_stats);
        if (r != STARK_OK) {
            throw Error("Failed to get stats");
        }
        return Stats(c_stats);
    }
    
    std::string get_last_error() {
        if (!db) return "Database not open";
        const char* err = stark_error(db);
        return err ? std::string(err) : "";
    }
};

} // namespace stark

#endif // STARK_CPP_HPP