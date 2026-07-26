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

typedef enum {
    STARK_DTYPE_FLOAT32 = 0,
    STARK_DTYPE_FLOAT64 = 1,
    STARK_DTYPE_INT32 = 2,
    STARK_DTYPE_INT64 = 3,
    STARK_DTYPE_UINT8 = 4,
    STARK_DTYPE_INT8 = 5,
    STARK_DTYPE_INT16 = 6
} stark_dtype_t;

// ==================== LIFECYCLE ====================

STARK_API stark_db_t* stark_open(const char* path, unsigned flags);
STARK_API void stark_close(stark_db_t* db);

// ==================== CRUD OPERATIONS ====================

STARK_API stark_result_t stark_put(stark_db_t* db, uint32_t key,
                                   const void* value, size_t value_size);

STARK_API stark_result_t stark_add(stark_db_t* db, uint32_t key,
                                   const void* value, size_t value_size);

STARK_API stark_result_t stark_get(stark_db_t* db, uint32_t key,
                                   void* buffer, size_t* buffer_size);

STARK_API stark_result_t stark_delete(stark_db_t* db, uint32_t key);

STARK_API int stark_exists(stark_db_t* db, uint32_t key);

// ==================== ITERATION ====================

typedef struct stark_cursor stark_cursor_t;

STARK_API stark_cursor_t* stark_cursor_create(stark_db_t* db);
STARK_API stark_result_t stark_cursor_first(stark_cursor_t* cursor);
STARK_API stark_result_t stark_cursor_last(stark_cursor_t* cursor);
STARK_API stark_result_t stark_cursor_next(stark_cursor_t* cursor);
STARK_API stark_result_t stark_cursor_prev(stark_cursor_t* cursor);
STARK_API stark_result_t stark_cursor_get(stark_cursor_t* cursor,
                                         uint32_t* key,
                                         void* buffer, size_t* buffer_size);
STARK_API void stark_cursor_destroy(stark_cursor_t* cursor);

// ==================== BINARY ARRAY STORAGE ====================

/**
 * Store a contiguous binary array (e.g., float embeddings, model inputs)
 * @param db Database handle
 * @param key Integer key
 * @param dtype Data type enum (STARK_DTYPE_FLOAT32, etc.)
 * @param data Pointer to the array data
 * @param elem_size Size of each element in bytes
 * @param count Number of elements
 * @return STARK_OK on success
 */
STARK_API stark_result_t stark_store_binary(stark_db_t* db, uint32_t key,
                                            stark_dtype_t dtype,
                                            const void* data,
                                            size_t elem_size,
                                            size_t count);

/**
 * Load a binary array from storage
 * @param db Database handle
 * @param key Integer key
 * @param dtype Output data type
 * @param buffer Output buffer (caller allocated)
 * @param count In/out: max count on input, actual count on output
 * @param elem_size Output element size
 * @return STARK_OK on success
 */
STARK_API stark_result_t stark_load_binary(stark_db_t* db, uint32_t key,
                                           stark_dtype_t* dtype,
                                           void* buffer,
                                           size_t* count,
                                           size_t* elem_size);

/**
 * Get a direct memory pointer to binary array data (ZERO-COPY)
 * The pointer is valid until the database is modified or closed.
 * @param db Database handle
 * @param key Integer key
 * @param dtype Output data type
 * @param ptr Output pointer to raw data
 * @param count Output number of elements
 * @param elem_size Output element size
 * @return STARK_OK on success
 */
STARK_API stark_result_t stark_get_binary_ptr(stark_db_t* db, uint32_t key,
                                              stark_dtype_t* dtype,
                                              void** ptr,
                                              size_t* count,
                                              size_t* elem_size);

// ==================== ARROW COLUMN LAYOUT ====================

typedef struct {
    char* schema_json;
    void** column_data;
    size_t* column_sizes;
    size_t num_columns;
    size_t num_rows;
} stark_column_data_t;

/**
 * Store data in Arrow columnar layout
 * @param db Database handle
 * @param column_key String key for the column set
 * @param schema_json JSON schema describing columns
 * @param columns Array of pointers to column data
 * @param column_sizes Size of each column in bytes
 * @param num_columns Number of columns
 * @param num_rows Number of rows
 * @return STARK_OK on success
 */
STARK_API stark_result_t stark_store_columns(stark_db_t* db,
                                             const char* column_key,
                                             const char* schema_json,
                                             void** columns,
                                             size_t* column_sizes,
                                             size_t num_columns,
                                             size_t num_rows);

/**
 * Load data stored in Arrow columnar layout
 * Result must be freed with stark_free_columns()
 * @param db Database handle
 * @param column_key String key for the column set
 * @param out Output column_data_t structure (caller must free with stark_free_columns)
 * @return STARK_OK on success
 */
STARK_API stark_result_t stark_load_columns(stark_db_t* db,
                                            const char* column_key,
                                            stark_column_data_t* out);

/**
 * Free memory allocated by stark_load_columns()
 */
STARK_API void stark_free_columns(stark_column_data_t* data);

// ==================== STRING KEY OPERATIONS ====================

STARK_API stark_result_t stark_put_str(stark_db_t* db, const char* key,
                                       const void* value, size_t value_size);
STARK_API stark_result_t stark_get_str(stark_db_t* db, const char* key,
                                       void* buffer, size_t* buffer_size);
STARK_API stark_result_t stark_del_str(stark_db_t* db, const char* key);
STARK_API int stark_exists_str(stark_db_t* db, const char* key);

// ==================== BATCH OPERATIONS ====================

typedef struct {
    uint32_t* keys;
    void** values;
    size_t* sizes;
    size_t count;
} stark_batch_t;

STARK_API stark_result_t stark_put_batch(stark_db_t* db, stark_batch_t* batch);
STARK_API void stark_free_batch(stark_batch_t* batch);

// ==================== STATISTICS ====================

typedef struct {
    uint64_t keys_count;
    uint32_t btree_height;
    uint64_t data_size;
    uint32_t page_count;
} stark_stats_t;

STARK_API stark_result_t stark_stats(stark_db_t* db, stark_stats_t* stats);

// ==================== UTILITIES ====================

STARK_API const char* stark_error(stark_db_t* db);
STARK_API stark_result_t stark_sync(stark_db_t* db);

// ==================== TYPE SYSTEM ====================

STARK_API stark_result_t stark_define_type(stark_db_t* db, const char* name,
                                           FieldDef* fields, uint32_t field_count);
STARK_API stark_result_t stark_undefine_type(stark_db_t* db, const char* name);
STARK_API TypeDef* stark_get_type(stark_db_t* db, const char* name);
STARK_API stark_result_t stark_list_types(stark_db_t* db, char*** names, uint32_t* count);
STARK_API stark_result_t stark_add_typed(stark_db_t* db, const char* type_name,
                                         uint32_t key, const char* field_values);
STARK_API stark_result_t stark_get_typed(stark_db_t* db, const char* type_name,
                                         uint32_t key, char* output, size_t output_size);

// ==================== TRANSACTIONS ====================

STARK_API stark_result_t stark_begin(stark_db_t* db);
STARK_API stark_result_t stark_commit(stark_db_t* db);
STARK_API stark_result_t stark_rollback(stark_db_t* db);
STARK_API int stark_in_transaction(stark_db_t* db);

#ifdef __cplusplus
}
#endif

#endif // STARK_H
