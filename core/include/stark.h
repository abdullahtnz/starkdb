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
    STARK_MEMORY_ERROR = -7,
    STARK_STALE_CURSOR = -8,
    STARK_BUFFER_TOO_SMALL = -9
} stark_result_t;

// Open flags accepted by stark_open()/stark_open_ex().
#define STARK_OPEN_CREATE   0x0u  // create the database if it does not exist
#define STARK_OPEN_EXCL     0x1u  // fail if the database does not exist
#define STARK_OPEN_READONLY 0x2u  // acquire a shared lock and reject all writes

typedef enum {
    STARK_OPEN_OK              = 0,
    STARK_OPEN_ERR_LOCKED      = 1,   // held by another process
    STARK_OPEN_ERR_NOT_FOUND   = 2,   // STARK_OPEN_EXCL and DB missing
    STARK_OPEN_ERR_READONLY    = 3,   // STARK_OPEN_EXCL on a read-only file
    STARK_OPEN_ERR_CORRUPT     = 4,   // bad magic or size
    STARK_OPEN_ERR_VERSION     = 5,   // file version too new
    STARK_OPEN_ERR_IO          = 6,   // permission, disk full, etc.
    STARK_OPEN_ERR_INVALID_ARG = 7    // NULL path, etc.
} stark_open_error_t;

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

/**
 * Open (or create) a STARKDB database.
 *
 * Both the index (.idx) and data (.dat) files start with a 16-byte header:
 *   magic "STARKDB\0" (8 bytes), uint32 version, uint32 flags (0).
 * The current on-disk format version is 1.  Files with a newer version are
 * rejected.  New files are created with the header written immediately.
 *
 * An exclusive flock() is held on <path>.idx for the lifetime of the handle.
 * flags is a bitwise OR of STARK_OPEN_CREATE (0), STARK_OPEN_EXCL and
 * STARK_OPEN_READONLY.  When another process holds the database lock this
 * function fails immediately (it never blocks).
 *
 * @param path Base path (without extension); creates <path>.idx and <path>.dat
 * @param flags STARK_OPEN_* flags (0 = STARK_OPEN_CREATE)
 * @return Database handle, or NULL on failure.  Use stark_last_open_error()
 *         for the reason.  This function is not thread-safe with respect to
 *         that error string.
 */
STARK_API stark_db_t* stark_open(const char* path, unsigned flags);

/**
 * Open a database and report a structured error code.
 *
 * On success *out_db is a valid handle, *out_err is STARK_OPEN_OK and
 * STARK_OK is returned.  On failure *out_db is NULL, *out_err receives the
 * specific reason and STARK_ERROR is returned.  A human-readable description
 * is available from stark_last_open_error().
 *
 * @param out_db Receives the database handle (must not be NULL)
 * @param path Base path (without extension)
 * @param flags STARK_OPEN_* flags
 * @param out_err Receives the structured error code (must not be NULL)
 * @return STARK_OK on success, STARK_ERROR on failure, STARK_INVALID_ARG for
 *         NULL arguments
 */
STARK_API stark_result_t stark_open_ex(stark_db_t** out_db, const char* path,
                                       uint32_t flags, stark_open_error_t* out_err);

/**
 * Describe the most recent failed stark_open()/stark_open_ex().
 *
 * Returns a static string, or NULL if the last open attempt succeeded.
 * NOT THREAD-SAFE: the string is stored in a single static buffer.
 */
STARK_API const char* stark_last_open_error(void);

STARK_API void stark_close(stark_db_t* db);

/**
 * Copy the currently open database to dest_prefix.
 *
 * Flushes the database first, then atomically (same-filesystem) installs
 * <dest_prefix>.idx and <dest_prefix>.dat.  Stale <dest_prefix>.*.tmp files
 * from an interrupted operation are removed and the backup is retried in the
 * same call.  Fails if a transaction is active, if source and destination are
 * the same database, or if the destination is in use by another process.
 * The destination must be on the same filesystem for atomic renames; on
 * EXDEV a non-atomic copy fallback is used.
 *
 * @return STARK_OK on success, STARK_ERROR / STARK_IO_ERROR on failure with
 *         db->last_error set.
 */
STARK_API stark_result_t stark_backup(stark_db_t* db, const char* dest_prefix);

/**
 * Replace dest_prefix with a cold copy of src_prefix.
 *
 * The source is header-validated but NOT locked, so it must not be written by
 * another process while this runs.  The destination is locked if it exists.
 * The replacement is whole-database (no merge) and uses the same temp-file +
 * rename scheme as stark_backup().  Because there is no database handle, this
 * function cannot set db->last_error; inspect the returned result code.
 *
 * @return STARK_OK on success, STARK_ERROR for invalid/incomplete sources or
 *         a busy/invalid destination, STARK_IO_ERROR on copy/rename failure.
 */
STARK_API stark_result_t stark_restore(const char* src_prefix, const char* dest_prefix);

/**
 * Upgrade a pre-header (legacy) database to the current file format.
 *
 * For each of <path>.idx and <path>.dat: if the file already has a valid
 * header it is left untouched; if it is a legacy file (size is a multiple of
 * the page size and has no header), a new file containing the header followed
 * by the original bytes is written and atomically renamed over the original.
 *
 * Must be called while the database is closed.
 *
 * @param path Base path (without extension)
 * @return STARK_OK on success, STARK_ERROR on failure
 */
STARK_API stark_result_t stark_upgrade_file(const char* path);

// ==================== CRUD OPERATIONS ====================

STARK_API stark_result_t stark_put(stark_db_t* db, uint32_t key,
                                   const void* value, size_t value_size);

STARK_API stark_result_t stark_add(stark_db_t* db, uint32_t key,
                                   const void* value, size_t value_size);

/**
 * Get a value by numeric key.
 *
 * *buffer_size is in/out: pass the buffer capacity; on success it receives the
 * actual value size.  If the buffer is NULL or too small, the required size is
 * written to *buffer_size and STARK_BUFFER_TOO_SMALL is returned.
 */
STARK_API stark_result_t stark_get(stark_db_t* db, uint32_t key,
                                   void* buffer, size_t* buffer_size);

STARK_API stark_result_t stark_delete(stark_db_t* db, uint32_t key);

STARK_API int stark_exists(stark_db_t* db, uint32_t key);

// ==================== ITERATION ====================

typedef struct stark_cursor stark_cursor_t;

/**
 * Create a cursor over all keys (ascending order).
 *
 * Cursors are invalidated by any modification to the database: after a write
 * (put/add/delete/typed add/undefine/binary/columns/batch/compact) any cursor
 * created earlier returns STARK_STALE_CURSOR from its operations. After the
 * database is closed, cursor operations return STARK_CLOSED. Re-create the
 * cursor to continue iterating.
 */
STARK_API stark_cursor_t* stark_cursor_create(stark_db_t* db);
STARK_API stark_result_t stark_cursor_first(stark_cursor_t* cursor);
STARK_API stark_result_t stark_cursor_last(stark_cursor_t* cursor);
STARK_API stark_result_t stark_cursor_next(stark_cursor_t* cursor);
STARK_API stark_result_t stark_cursor_prev(stark_cursor_t* cursor);
/**
 * Get the current cursor entry.  Like stark_get(), a NULL or too-small buffer
 * yields STARK_BUFFER_TOO_SMALL with the required size in *buffer_size.
 */
STARK_API stark_result_t stark_cursor_get(stark_cursor_t* cursor,
                                         uint32_t* key,
                                         void* buffer, size_t* buffer_size);
STARK_API void stark_cursor_destroy(stark_cursor_t* cursor);

/**
 * Callback for stark_foreach_range().  value points into database memory and
 * is valid only for the duration of the call.  Return non-zero to stop early.
 */
typedef int (*stark_foreach_cb)(uint32_t key, const void* value,
                                size_t value_size, void* user);

/**
 * Iterate raw B-tree keys in the half-open range [key_min, key_max).
 *
 * Keys are visited in ascending order.  djb2 hashes of string keys that fall
 * inside the range are included; choose ranges that hashes are unlikely to
 * hit if you need only your own numeric keys.  Returning non-zero from cb
 * stops early and still returns STARK_OK.  Writing to the database from cb
 * aborts iteration with STARK_ERROR ("foreach: modified during iteration").
 * The callback must not hold the value pointer after it returns.
 *
 * @return STARK_OK on completion (including early stop), STARK_ERROR on
 *         modification or internal failure, STARK_CLOSED if the DB was closed.
 */
STARK_API stark_result_t stark_foreach_range(stark_db_t* db,
                                             uint32_t key_min, uint32_t key_max,
                                             stark_foreach_cb cb, void* user);

// ==================== BINARY ARRAY STORAGE ====================

/**
 * Store a contiguous binary array (e.g., float embeddings, model inputs).
 * The element dtype is stored explicitly in the value header, so arrays
 * round-trip with their exact dtype (including int32 vs float32). Arrays
 * written by older versions are still readable (dtype inferred from size).
 * @param db Database handle
 * @param key Integer key
 * @param dtype Data type enum (STARK_DTYPE_FLOAT32, etc.)
 * @param data Pointer to the array data (may be NULL when count == 0)
 * @param elem_size Size of each element in bytes (0 = derive from dtype)
 * @param count Number of elements
 * @return STARK_OK on success
 */
STARK_API stark_result_t stark_store_binary(stark_db_t* db, uint32_t key,
                                            stark_dtype_t dtype,
                                            const void* data,
                                            size_t elem_size,
                                            size_t count);

/**
 * Load a binary array from storage.
 *
 * *count is in/out: pass the buffer capacity in elements; on success it
 * receives the actual count.  A NULL or undersized buffer returns
 * STARK_BUFFER_TOO_SMALL and writes the required count to *count.
 *
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
/**
 * Get a value by string key.  Uses the same in/out buffer semantics as
 * stark_get(): NULL/undersized buffers return STARK_BUFFER_TOO_SMALL.
 */
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
    uint64_t keys_count;   // exact number of keys across all B-tree leaves
    uint32_t btree_height; // 1 for a single leaf root, 2 for root+children, ...
    uint64_t data_size;
    uint32_t page_count;
} stark_stats_t;

/**
 * Get database statistics. keys_count and btree_height are exact and account
 * for multi-page B-trees.
 */
STARK_API stark_result_t stark_stats(stark_db_t* db, stark_stats_t* stats);

// ==================== UTILITIES ====================

STARK_API const char* stark_error(stark_db_t* db);
STARK_API stark_result_t stark_sync(stark_db_t* db);

// ==================== COMPACTION ====================

/**
 * Compact the database: rewrite the data file with only live values and
 * rebuild the index to point at the new offsets, reclaiming space left by
 * overwritten/deleted keys. The swap is crash-safe (uses a <path>.compact
 * sentinel and rename) and preserves the cross-process file lock across the
 * swap. Returns STARK_ERROR if called during a transaction or on a read-only
 * handle. Cursors opened before compaction become stale.
 */
STARK_API stark_result_t stark_compact(stark_db_t* db);

/**
 * Compact the database and report total on-disk bytes before and after.
 * Either output pointer may be NULL.
 */
STARK_API stark_result_t stark_compact_stats(stark_db_t* db,
                                             uint64_t* before_bytes,
                                             uint64_t* after_bytes);

// ==================== TYPE SYSTEM ====================

STARK_API stark_result_t stark_define_type(stark_db_t* db, const char* name,
                                           FieldDef* fields, uint32_t field_count);

/**
 * Delete a type definition AND all of its records.
 *
 * Records are tracked in a per-type manifest, so undefining a type removes
 * every "<name>:<key>" record as well as the type definition. Types defined by
 * older versions (with no manifest) only lose their definition; a warning is
 * placed in stark_error() and any pre-existing records are left orphaned.
 */
STARK_API stark_result_t stark_undefine_type(stark_db_t* db, const char* name);
STARK_API TypeDef* stark_get_type(stark_db_t* db, const char* name);
STARK_API stark_result_t stark_list_types(stark_db_t* db, char*** names, uint32_t* count);
STARK_API stark_result_t stark_add_typed(stark_db_t* db, const char* type_name,
                                         uint32_t key, const char* field_values);

/**
 * Get a typed record formatted as "field=value ...".
 *
 * If output_size is too small, STARK_BUFFER_TOO_SMALL is returned and
 * db->last_error contains the exact required size.
 */
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
