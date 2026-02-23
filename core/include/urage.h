#ifndef URAGE_H
#define URAGE_H

#include <stdint.h>
#include <stddef.h>

#ifdef _WIN32
    #ifdef URAGE_BUILD_SHARED
        #define URAGE_API __declspec(dllexport)
    #else
        #define URAGE_API __declspec(dllimport)
    #endif
#else
    #define URAGE_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle - users never see inside
typedef struct urage_db urage_db_t;

// Result codes
typedef enum {
    URAGE_OK = 0,
    URAGE_ERROR = -1,
    URAGE_NOT_FOUND = -2,
    URAGE_FULL = -3,
    URAGE_IO_ERROR = -4,
    URAGE_INVALID_ARG = -5,
    URAGE_CLOSED = -6
} urage_result_t;

// ==================== LIFECYCLE ====================

/**
 * Open a database connection
 * @param path Database file path (without extension)
 * @param flags Reserved for future use
 * @return Database handle or NULL on error
 */
URAGE_API urage_db_t* urage_open(const char* path, unsigned flags);

/**
 * Close database and flush all changes
 * @param db Database handle
 */
URAGE_API void urage_close(urage_db_t* db);

// ==================== CRUD OPERATIONS ====================

/**
 * Insert or update a key-value pair
 * @param db Database handle
 * @param key 32-bit integer key
 * @param value Data to store
 * @param value_size Size of value in bytes
 * @return URAGE_OK on success
 */
URAGE_API urage_result_t urage_put(urage_db_t* db, uint32_t key, 
                                   const void* value, size_t value_size);

/**
 * Get value by key
 * @param db Database handle
 * @param key Key to find
 * @param buffer Output buffer
 * @param buffer_size Size of buffer (will be set to actual size)
 * @return URAGE_OK if found, URAGE_NOT_FOUND if not
 */
URAGE_API urage_result_t urage_get(urage_db_t* db, uint32_t key,
                                   void* buffer, size_t* buffer_size);

/**
 * Delete a key-value pair
 * @param db Database handle
 * @param key Key to delete
 * @return URAGE_OK if deleted, URAGE_NOT_FOUND if not
 */
URAGE_API urage_result_t urage_delete(urage_db_t* db, uint32_t key);

/**
 * Check if key exists
 * @param db Database handle
 * @param key Key to check
 * @return 1 if exists, 0 if not
 */
URAGE_API int urage_exists(urage_db_t* db, uint32_t key);

// ==================== ITERATION ====================

// Opaque cursor handle
typedef struct urage_cursor urage_cursor_t;

/**
 * Create a cursor for iterating over database
 * @param db Database handle
 * @return Cursor handle or NULL on error
 */
URAGE_API urage_cursor_t* urage_cursor_create(urage_db_t* db);

/**
 * Move cursor to first key
 * @param cursor Cursor handle
 * @return URAGE_OK if exists, URAGE_NOT_FOUND if empty
 */
URAGE_API urage_result_t urage_cursor_first(urage_cursor_t* cursor);

/**
 * Move cursor to last key
 * @param cursor Cursor handle
 * @return URAGE_OK if exists, URAGE_NOT_FOUND if empty
 */
URAGE_API urage_result_t urage_cursor_last(urage_cursor_t* cursor);

/**
 * Move cursor to next key
 * @param cursor Cursor handle
 * @return URAGE_OK if exists, URAGE_NOT_FOUND if at end
 */
URAGE_API urage_result_t urage_cursor_next(urage_cursor_t* cursor);

/**
 * Move cursor to previous key
 * @param cursor Cursor handle
 * @return URAGE_OK if exists, URAGE_NOT_FOUND if at start
 */
URAGE_API urage_result_t urage_cursor_prev(urage_cursor_t* cursor);

/**
 * Get current key and value at cursor
 * @param cursor Cursor handle
 * @param key Output key
 * @param buffer Output buffer
 * @param buffer_size Size of buffer (will be set to actual size)
 * @return URAGE_OK on success
 */
URAGE_API urage_result_t urage_cursor_get(urage_cursor_t* cursor,
                                         uint32_t* key,
                                         void* buffer, size_t* buffer_size);

/**
 * Destroy cursor
 * @param cursor Cursor handle
 */
URAGE_API void urage_cursor_destroy(urage_cursor_t* cursor);

// ==================== STATISTICS ====================

typedef struct {
    uint64_t keys_count;      // Number of keys
    uint32_t btree_height;     // B-tree height
    uint64_t data_size;        // Total data size in bytes
    uint32_t page_count;       // Number of pages used
} urage_stats_t;

/**
 * Get database statistics
 * @param db Database handle
 * @param stats Output statistics
 * @return URAGE_OK on success
 */
URAGE_API urage_result_t urage_stats(urage_db_t* db, urage_stats_t* stats);

// ==================== UTILITIES ====================

/**
 * Get last error message
 * @param db Database handle
 * @return Error string (do not free)
 */
URAGE_API const char* urage_error(urage_db_t* db);

/**
 * Flush all changes to disk
 * @param db Database handle
 * @return URAGE_OK on success
 */
URAGE_API urage_result_t urage_sync(urage_db_t* db);

#ifdef __cplusplus
}
#endif

#endif // URAGE_H