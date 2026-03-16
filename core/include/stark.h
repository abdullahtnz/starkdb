#ifndef STARK_H
#define STARK_H

#include <stdint.h>
#include <stddef.h>
#include "type.h"

#ifdef _WIN32
    #ifdef STARK_BUILD_SHARED
        #define STARK_API __declspec(dllexport)
    #else
        #define STARK_API __declspec(dllimport)
    #endif
#else
    #define STARK_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle - users never see inside
typedef struct stark_db stark_db_t;

// Result codes
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

// ==================== LIFECYCLE ====================

/**
 * Open a database connection
 * @param path Database file path (without extension)
 * @param flags Reserved for future use
 * @return Database handle or NULL on error
 */
STARK_API stark_db_t* stark_open(const char* path, unsigned flags);

/**
 * Close database and flush all changes
 * @param db Database handle
 */
STARK_API void stark_close(stark_db_t* db);

// ==================== CRUD OPERATIONS ====================

/**
 * Insert or update a key-value pair
 * @param db Database handle
 * @param key 32-bit integer key
 * @param value Data to store
 * @param value_size Size of value in bytes
 * @return STARK_OK on success
 */
STARK_API stark_result_t stark_put(stark_db_t* db, uint32_t key, 
                                   const void* value, size_t value_size);

STARK_API stark_result_t stark_add(stark_db_t* db, uint32_t key, 
                                   const void* value, size_t value_size);
/**
 * Get value by key
 * @param db Database handle
 * @param key Key to find
 * @param buffer Output buffer
 * @param buffer_size Size of buffer (will be set to actual size)
 * @return STARK_OK if found, STARK_NOT_FOUND if not
 */
STARK_API stark_result_t stark_get(stark_db_t* db, uint32_t key,
                                   void* buffer, size_t* buffer_size);

/**
 * Delete a key-value pair
 * @param db Database handle
 * @param key Key to delete
 * @return STARK_OK if deleted, STARK_NOT_FOUND if not
 */
STARK_API stark_result_t stark_delete(stark_db_t* db, uint32_t key);

/**
 * Check if key exists
 * @param db Database handle
 * @param key Key to check
 * @return 1 if exists, 0 if not
 */
STARK_API int stark_exists(stark_db_t* db, uint32_t key);

// ==================== ITERATION ====================

// Opaque cursor handle
typedef struct stark_cursor stark_cursor_t;

/**
 * Create a cursor for iterating over database
 * @param db Database handle
 * @return Cursor handle or NULL on error
 */
STARK_API stark_cursor_t* stark_cursor_create(stark_db_t* db);

/**
 * Move cursor to first key
 * @param cursor Cursor handle
 * @return STARK_OK if exists, STARK_NOT_FOUND if empty
 */
STARK_API stark_result_t stark_cursor_first(stark_cursor_t* cursor);

/**
 * Move cursor to last key
 * @param cursor Cursor handle
 * @return STARK_OK if exists, STARK_NOT_FOUND if empty
 */
STARK_API stark_result_t stark_cursor_last(stark_cursor_t* cursor);

/**
 * Move cursor to next key
 * @param cursor Cursor handle
 * @return STARK_OK if exists, STARK_NOT_FOUND if at end
 */
STARK_API stark_result_t stark_cursor_next(stark_cursor_t* cursor);

/**
 * Move cursor to previous key
 * @param cursor Cursor handle
 * @return STARK_OK if exists, STARK_NOT_FOUND if at start
 */
STARK_API stark_result_t stark_cursor_prev(stark_cursor_t* cursor);

/**
 * Get current key and value at cursor
 * @param cursor Cursor handle
 * @param key Output key
 * @param buffer Output buffer
 * @param buffer_size Size of buffer (will be set to actual size)
 * @return STARK_OK on success
 */
STARK_API stark_result_t stark_cursor_get(stark_cursor_t* cursor,
                                         uint32_t* key,
                                         void* buffer, size_t* buffer_size);

/**
 * Destroy cursor
 * @param cursor Cursor handle
 */
STARK_API void stark_cursor_destroy(stark_cursor_t* cursor);



// ==================== STRING KEY OPERATIONS ====================

/**
 * Insert or update a key-value pair with string key
 * @param db Database handle
 * @param key String key (will be hashed internally)
 * @param value Data to store
 * @param value_size Size of value in bytes
 * @return STARK_OK on success
 */
STARK_API stark_result_t stark_put_str(stark_db_t* db, const char* key,
                                       const void* value, size_t value_size);

/**
 * Get value by string key
 * @param db Database handle
 * @param key String key
 * @param buffer Output buffer
 * @param buffer_size Size of buffer (will be set to actual size)
 * @return STARK_OK if found, STARK_NOT_FOUND if not
 */
STARK_API stark_result_t stark_get_str(stark_db_t* db, const char* key,
                                       void* buffer, size_t* buffer_size);

/**
 * Delete a key-value pair with string key
 * @param db Database handle
 * @param key String key
 * @return STARK_OK if deleted, STARK_NOT_FOUND if not
 */
STARK_API stark_result_t stark_del_str(stark_db_t* db, const char* key);

/**
 * Check if string key exists
 * @param db Database handle
 * @param key String key
 * @return 1 if exists, 0 if not
 */
STARK_API int stark_exists_str(stark_db_t* db, const char* key);

// ==================== STATISTICS ====================

typedef struct {
    uint64_t keys_count;      // Number of keys
    uint32_t btree_height;     // B-tree height
    uint64_t data_size;        // Total data size in bytes
    uint32_t page_count;       // Number of pages used
} stark_stats_t;

/**
 * Get database statistics
 * @param db Database handle
 * @param stats Output statistics
 * @return STARK_OK on success
 */
STARK_API stark_result_t stark_stats(stark_db_t* db, stark_stats_t* stats);

// ==================== UTILITIES ====================

/**
 * Get last error message
 * @param db Database handle
 * @return Error string (do not free)
 */
STARK_API const char* stark_error(stark_db_t* db);

/**
 * Flush all changes to disk
 * @param db Database handle
 * @return STARK_OK on success
 */
STARK_API stark_result_t stark_sync(stark_db_t* db);

// ==================== TYPE SYSTEM ====================

/**
 * Define a new struct type
 * @param db Database handle
 * @param name Type name
 * @param fields Array of field definitions
 * @param field_count Number of fields
 * @return STARK_OK on success
 */
STARK_API stark_result_t stark_define_type(stark_db_t* db, const char* name,
                                           FieldDef* fields, uint32_t field_count);

/**
 * Delete a type definition
 * @param db Database handle
 * @param name Type name
 * @return STARK_OK on success
 */
STARK_API stark_result_t stark_undefine_type(stark_db_t* db, const char* name);

/**
 * Get type information
 * @param db Database handle
 * @param name Type name
 * @return TypeDef pointer (must be freed) or NULL if not found
 */
STARK_API TypeDef* stark_get_type(stark_db_t* db, const char* name);

/**
 * List all defined types
 * @param db Database handle
 * @param names Output array of type names (caller must free)
 * @param count Output count
 * @return STARK_OK on success
 */
STARK_API stark_result_t stark_list_types(stark_db_t* db, char*** names, uint32_t* count);

/**
 * Add data using a defined type
 * @param db Database handle
 * @param type_name Type name
 * @param key Key value
 * @param field_values String with field=value pairs
 * @return STARK_OK on success
 */
STARK_API stark_result_t stark_add_typed(stark_db_t* db, const char* type_name,
                                         uint32_t key, const char* field_values);

/**
 * Get data using a defined type
 * @param db Database handle
 * @param type_name Type name
 * @param key Key value
 * @param output Buffer for formatted output
 * @param output_size Size of output buffer
 * @return STARK_OK on success
 */
STARK_API stark_result_t stark_get_typed(stark_db_t* db, const char* type_name,
                                         uint32_t key, char* output, size_t output_size);

                                         

#ifdef __cplusplus
}

// ==================== TRANSACTIONS ====================

/**
 * Begin a transaction
 * All subsequent operations will be atomic
 * @param db Database handle
 * @return STARK_OK on success
 */
STARK_API stark_result_t stark_begin(stark_db_t* db);

/**
 * Commit current transaction
 * Makes all changes permanent
 * @param db Database handle
 * @return STARK_OK on success
 */
STARK_API stark_result_t stark_commit(stark_db_t* db);

/**
 * Rollback current transaction
 * Discards all changes since begin
 * @param db Database handle
 * @return STARK_OK on success
 */
STARK_API stark_result_t stark_rollback(stark_db_t* db);

/**
 * Check if in transaction
 * @param db Database handle
 * @return 1 if in transaction, 0 if not
 */
STARK_API int stark_in_transaction(stark_db_t* db);

#endif

#endif // STARK_H