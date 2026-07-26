#include "stark.h"
#include "database.h"
#include "btree.h"
#include "storage.h"
#include "type.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct stark_db {
    Database* internal_db;
    char last_error[256];
    char* path;
    int in_transaction;
    void* transaction_log;
    size_t log_size;
};

// ==================== LIFECYCLE ====================

STARK_API stark_db_t* stark_open(const char* path, unsigned flags) {
    (void)flags;
    stark_db_t* db = (stark_db_t*)calloc(1, sizeof(stark_db_t));
    if (!db) return NULL;
    db->path = strdup(path);
    db->internal_db = db_open(path);
    if (!db->internal_db) {
        snprintf(db->last_error, sizeof(db->last_error),
                 "Failed to open database: %s", path);
        free(db->path);
        free(db);
        return NULL;
    }
    return db;
}

STARK_API void stark_close(stark_db_t* db) {
    if (!db) return;
    if (db->internal_db) {
        stark_sync(db);
        db_close(db->internal_db);
    }
    free(db->path);
    free(db);
    printf("Database synced and closed.\n");
}

// ==================== CRUD ====================

STARK_API stark_result_t stark_put(stark_db_t* db, uint32_t key,
                                    const void* value, size_t value_size) {
    return stark_add(db, key, value, value_size);
}

STARK_API stark_result_t stark_add(stark_db_t* db, uint32_t key,
                                    const void* value, size_t value_size) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (!value || value_size == 0) return STARK_INVALID_ARG;

    if (db->in_transaction) {
        printf("Logging change for key %u in transaction\n", key);
    }

    DB_Result result = db_insert(db->internal_db, key, value, value_size);

    switch (result) {
        case DB_SUCCESS: return STARK_OK;
        case DB_FULL: return STARK_FULL;
        case DB_IO_ERROR: return STARK_IO_ERROR;
        case DB_MEMORY_ERROR: return STARK_MEMORY_ERROR;
        default: return STARK_ERROR;
    }
}

STARK_API stark_result_t stark_get(stark_db_t* db, uint32_t key,
                                    void* buffer, size_t* buffer_size) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (!buffer || !buffer_size) return STARK_INVALID_ARG;

    DB_Result result = db_find(db->internal_db, key, buffer, buffer_size);

    switch (result) {
        case DB_SUCCESS: return STARK_OK;
        case DB_NOT_FOUND: return STARK_NOT_FOUND;
        case DB_ERROR: return STARK_ERROR;
        default: return STARK_ERROR;
    }
}

STARK_API stark_result_t stark_delete(stark_db_t* db, uint32_t key) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    DB_Result result = db_delete(db->internal_db, key);
    switch (result) {
        case DB_SUCCESS: return STARK_OK;
        case DB_NOT_FOUND: return STARK_NOT_FOUND;
        default: return STARK_ERROR;
    }
}

STARK_API int stark_exists(stark_db_t* db, uint32_t key) {
    if (!db || !db->internal_db) return 0;
    char dummy[256];
    size_t size = sizeof(dummy);
    DB_Result result = db_find(db->internal_db, key, dummy, &size);
    return (result == DB_SUCCESS) || (result == DB_ERROR && size > 0);
}

// ==================== CURSOR ====================

struct stark_cursor {
    stark_db_t* db;
    BTreeCursor* btc;
    int valid;
};

STARK_API stark_cursor_t* stark_cursor_create(stark_db_t* db) {
    if (!db || !db->internal_db) return NULL;
    stark_cursor_t* cursor = (stark_cursor_t*)calloc(1, sizeof(stark_cursor_t));
    if (!cursor) return NULL;
    cursor->db = db;
    cursor->btc = btree_cursor_create(db->internal_db->index);
    if (!cursor->btc) {
        free(cursor);
        return NULL;
    }
    cursor->valid = 0;
    return cursor;
}

STARK_API stark_result_t stark_cursor_first(stark_cursor_t* cursor) {
    if (!cursor || !cursor->btc) return STARK_INVALID_ARG;
    DB_Result r = btree_cursor_first(cursor->btc);
    cursor->valid = (r == DB_SUCCESS);
    return (r == DB_SUCCESS) ? STARK_OK : STARK_NOT_FOUND;
}

STARK_API stark_result_t stark_cursor_last(stark_cursor_t* cursor) {
    if (!cursor || !cursor->btc) return STARK_INVALID_ARG;
    DB_Result r = btree_cursor_last(cursor->btc);
    cursor->valid = (r == DB_SUCCESS);
    return (r == DB_SUCCESS) ? STARK_OK : STARK_NOT_FOUND;
}

STARK_API stark_result_t stark_cursor_next(stark_cursor_t* cursor) {
    if (!cursor || !cursor->btc) return STARK_INVALID_ARG;
    DB_Result r = btree_cursor_next(cursor->btc);
    cursor->valid = (r == DB_SUCCESS);
    return (r == DB_SUCCESS) ? STARK_OK : STARK_NOT_FOUND;
}

STARK_API stark_result_t stark_cursor_prev(stark_cursor_t* cursor) {
    if (!cursor || !cursor->btc) return STARK_INVALID_ARG;
    DB_Result r = btree_cursor_prev(cursor->btc);
    cursor->valid = (r == DB_SUCCESS);
    return (r == DB_SUCCESS) ? STARK_OK : STARK_NOT_FOUND;
}

STARK_API stark_result_t stark_cursor_get(stark_cursor_t* cursor,
                                         uint32_t* key,
                                         void* buffer, size_t* buffer_size) {
    if (!cursor || !cursor->btc || !cursor->valid) return STARK_NOT_FOUND;
    if (!key || !buffer || !buffer_size) return STARK_INVALID_ARG;

    uint64_t value;
    DB_Result r = btree_cursor_get(cursor->btc, key, &value);
    if (r != DB_SUCCESS) return STARK_NOT_FOUND;

    return storage_read(cursor->db->internal_db->storage, value, buffer, buffer_size) == DB_SUCCESS
        ? STARK_OK : STARK_ERROR;
}

STARK_API void stark_cursor_destroy(stark_cursor_t* cursor) {
    if (!cursor) return;
    if (cursor->btc) btree_cursor_destroy(cursor->btc);
    free(cursor);
}

// ==================== BINARY ARRAY STORAGE ====================

static size_t dtype_size(stark_dtype_t dtype) {
    switch (dtype) {
        case STARK_DTYPE_FLOAT32: return 4;
        case STARK_DTYPE_FLOAT64: return 8;
        case STARK_DTYPE_INT32:   return 4;
        case STARK_DTYPE_INT64:   return 8;
        case STARK_DTYPE_UINT8:   return 1;
        case STARK_DTYPE_INT8:    return 1;
        case STARK_DTYPE_INT16:   return 2;
        default: return 0;
    }
}

STARK_API stark_result_t stark_store_binary(stark_db_t* db, uint32_t key,
                                            stark_dtype_t dtype,
                                            const void* data,
                                            size_t elem_size,
                                            size_t count) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (!data || count == 0) return STARK_INVALID_ARG;

    if (elem_size == 0) {
        elem_size = dtype_size(dtype);
        if (elem_size == 0) return STARK_INVALID_ARG;
    }

    data_addr_t addr;
    DB_Result result = db_insert_binary(db->internal_db, key, data, elem_size, count, &addr);
    if (result != DB_SUCCESS) return STARK_ERROR;
    return STARK_OK;
}

STARK_API stark_result_t stark_load_binary(stark_db_t* db, uint32_t key,
                                           stark_dtype_t* dtype,
                                           void* buffer,
                                           size_t* count,
                                           size_t* elem_size) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (!buffer || !count) return STARK_INVALID_ARG;

    DB_Result result = db_find_binary(db->internal_db, key, buffer, count, elem_size);
    if (result != DB_SUCCESS) {
        if (result == DB_NOT_FOUND) return STARK_NOT_FOUND;
        return STARK_ERROR;
    }

    if (dtype) {
        if (*elem_size == 4) *dtype = STARK_DTYPE_FLOAT32;
        else if (*elem_size == 8) *dtype = STARK_DTYPE_FLOAT64;
        else if (*elem_size == 2) *dtype = STARK_DTYPE_INT16;
        else if (*elem_size == 1) *dtype = STARK_DTYPE_UINT8;
        else *dtype = STARK_DTYPE_INT32;
    }

    return STARK_OK;
}

STARK_API stark_result_t stark_get_binary_ptr(stark_db_t* db, uint32_t key,
                                              stark_dtype_t* dtype,
                                              void** ptr,
                                              size_t* count,
                                              size_t* elem_size) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (!ptr) return STARK_INVALID_ARG;

    DB_Result result = db_get_binary_ptr(db->internal_db, key, ptr, count, elem_size);
    if (result != DB_SUCCESS) {
        if (result == DB_NOT_FOUND) return STARK_NOT_FOUND;
        return STARK_ERROR;
    }

    if (dtype) {
        if (elem_size && *elem_size == 4) *dtype = STARK_DTYPE_FLOAT32;
        else if (elem_size && *elem_size == 8) *dtype = STARK_DTYPE_FLOAT64;
        else if (elem_size && *elem_size == 2) *dtype = STARK_DTYPE_INT16;
        else if (elem_size && *elem_size == 1) *dtype = STARK_DTYPE_UINT8;
        else *dtype = STARK_DTYPE_INT32;
    }

    return STARK_OK;
}

// ==================== ARROW COLUMN LAYOUT ====================

STARK_API stark_result_t stark_store_columns(stark_db_t* db,
                                             const char* column_key,
                                             const char* schema_json,
                                             void** columns,
                                             size_t* column_sizes,
                                             size_t num_columns,
                                             size_t num_rows) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (!column_key || !schema_json || !columns || !column_sizes) return STARK_INVALID_ARG;

    DB_Result result = db_store_columns(db->internal_db, column_key, schema_json,
                                         columns, column_sizes, num_columns, num_rows);
    return (result == DB_SUCCESS) ? STARK_OK : STARK_ERROR;
}

STARK_API stark_result_t stark_load_columns(stark_db_t* db,
                                            const char* column_key,
                                            stark_column_data_t* out) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (!column_key || !out) return STARK_INVALID_ARG;

    memset(out, 0, sizeof(stark_column_data_t));

    DB_Result result = db_load_columns(db->internal_db, column_key,
                                        &out->schema_json,
                                        &out->column_data,
                                        &out->column_sizes,
                                        &out->num_columns,
                                        &out->num_rows);
    return (result == DB_SUCCESS) ? STARK_OK : STARK_ERROR;
}

STARK_API void stark_free_columns(stark_column_data_t* data) {
    if (!data) return;
    free(data->schema_json);
    free(data->column_sizes);
    free(data->column_data);
    memset(data, 0, sizeof(stark_column_data_t));
}

// ==================== STRING KEY HASHING ====================

static uint32_t hash_string(const char* str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

STARK_API stark_result_t stark_put_str(stark_db_t* db, const char* key,
                                        const void* value, size_t value_size) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (!key || !value || value_size == 0) return STARK_INVALID_ARG;
    uint32_t hashed_key = hash_string(key);
    printf("Debug: String key '%s' hashed to %u\n", key, hashed_key);
    return stark_add(db, hashed_key, value, value_size);
}

STARK_API stark_result_t stark_get_str(stark_db_t* db, const char* key,
                                        void* buffer, size_t* buffer_size) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (!key || !buffer_size) return STARK_INVALID_ARG;
    uint32_t hashed_key = hash_string(key);
    printf("Debug: String key '%s' hashed to %u\n", key, hashed_key);
    if (buffer == NULL) {
        char dummy[1];
        size_t dummy_size = 0;
        DB_Result result = db_find(db->internal_db, hashed_key, dummy, &dummy_size);
        if (result == DB_SUCCESS || (result == DB_ERROR && dummy_size > 0)) {
            *buffer_size = dummy_size;
            return STARK_ERROR;
        }
        return STARK_NOT_FOUND;
    }
    return stark_get(db, hashed_key, buffer, buffer_size);
}

STARK_API stark_result_t stark_del_str(stark_db_t* db, const char* key) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (!key) return STARK_INVALID_ARG;
    uint32_t hashed_key = hash_string(key);
    return stark_delete(db, hashed_key);
}

STARK_API int stark_exists_str(stark_db_t* db, const char* key) {
    if (!db || !db->internal_db) return 0;
    if (!key) return 0;
    uint32_t hashed_key = hash_string(key);
    return stark_exists(db, hashed_key);
}

// ==================== BATCH OPERATIONS ====================

STARK_API stark_result_t stark_put_batch(stark_db_t* db, stark_batch_t* batch) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (!batch || !batch->keys || !batch->values || !batch->sizes || batch->count == 0)
        return STARK_INVALID_ARG;

    for (size_t i = 0; i < batch->count; i++) {
        stark_result_t r = stark_add(db, batch->keys[i], batch->values[i], batch->sizes[i]);
        if (r != STARK_OK) return r;
    }
    return STARK_OK;
}

STARK_API void stark_free_batch(stark_batch_t* batch) {
    if (!batch) return;
    free(batch->keys);
    for (size_t i = 0; i < batch->count; i++) {
        free(batch->values[i]);
    }
    free(batch->values);
    free(batch->sizes);
    memset(batch, 0, sizeof(stark_batch_t));
}

// ==================== STATISTICS ====================

STARK_API stark_result_t stark_stats(stark_db_t* db, stark_stats_t* stats) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (!stats) return STARK_INVALID_ARG;

    Database* internal = db->internal_db;
    stats->page_count = internal->storage->pager->num_pages;
    stats->keys_count = btree_count_keys(internal->index);
    stats->btree_height = 1;
    stats->data_size = (uint64_t)internal->storage->current_page * PAGE_SIZE
                       + internal->storage->current_offset;

    return STARK_OK;
}

STARK_API const char* stark_error(stark_db_t* db) {
    if (!db) return "Database handle is NULL";
    return db->last_error;
}

STARK_API stark_result_t stark_sync(stark_db_t* db) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    Database* internal = db->internal_db;
    printf("Syncing to disk...\n");
    if (internal->index && internal->index->pager) {
        printf("  Flushing index pager (%u pages)\n", internal->index->pager->num_pages);
        pager_flush_all(internal->index->pager);
    }
    if (internal->storage && internal->storage->pager) {
        printf("  Flushing storage pager (%u pages)\n", internal->storage->pager->num_pages);
        pager_flush_all(internal->storage->pager);
    }
    printf("Synced to disk\n");
    return STARK_OK;
}

// ==================== TYPE SYSTEM IMPLEMENTATION ====================

extern stark_result_t type_create(stark_db_t* db, const char* name,
                                  FieldDef* fields, uint32_t field_count);
extern TypeDef* type_get(stark_db_t* db, const char* name);
extern stark_result_t type_delete(stark_db_t* db, const char* name);
extern stark_result_t type_list(stark_db_t* db, char*** names, uint32_t* count);
extern stark_result_t type_serialize(FieldDef* fields, uint32_t field_count,
                                     const char* field_values, void* buffer);
extern stark_result_t type_deserialize(FieldDef* fields, uint32_t field_count,
                                       const void* buffer, char* output, size_t output_size);

STARK_API stark_result_t stark_define_type(stark_db_t* db, const char* name,
                                           FieldDef* fields, uint32_t field_count) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (!name || !fields || field_count == 0) return STARK_INVALID_ARG;
    return type_create(db, name, fields, field_count);
}

STARK_API stark_result_t stark_undefine_type(stark_db_t* db, const char* name) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (!name) return STARK_INVALID_ARG;
    return type_delete(db, name);
}

STARK_API TypeDef* stark_get_type(stark_db_t* db, const char* name) {
    if (!db || !db->internal_db) return NULL;
    if (!name) return NULL;
    return type_get(db, name);
}

STARK_API stark_result_t stark_list_types(stark_db_t* db, char*** names, uint32_t* count) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (!names || !count) return STARK_INVALID_ARG;
    return type_list(db, names, count);
}

STARK_API stark_result_t stark_add_typed(stark_db_t* db, const char* type_name,
                                         uint32_t key, const char* field_values) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (!type_name || !field_values) return STARK_INVALID_ARG;

    TypeDef* type = type_get(db, type_name);
    if (!type) return STARK_NOT_FOUND;

    void* buffer = calloc(1, type->size);
    if (!buffer) {
        free(type);
        return STARK_MEMORY_ERROR;
    }

    stark_result_t result = type_serialize(type->fields, type->field_count,
                                           field_values, buffer);
    if (result == STARK_OK) {
        char data_key[256];
        snprintf(data_key, sizeof(data_key), "%s:%u", type_name, key);
        result = stark_put_str(db, data_key, buffer, type->size);
    }

    free(buffer);
    free(type);
    return result;
}

STARK_API stark_result_t stark_get_typed(stark_db_t* db, const char* type_name,
                                         uint32_t key, char* output, size_t output_size) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (!type_name || !output || output_size == 0) return STARK_INVALID_ARG;

    printf("Looking up type: '%s'\n", type_name);

    TypeDef* type = type_get(db, type_name);
    if (!type) {
        printf("Type '%s' not found in database\n", type_name);
        return STARK_NOT_FOUND;
    }

    printf("Found type: %s (ID: %u, size: %u bytes)\n", type->name, type->id, type->size);

    char data_key[256];
    snprintf(data_key, sizeof(data_key), "%s:%u", type_name, key);
    printf("Data key: %s\n", data_key);

    void* buffer = malloc(type->size);
    if (!buffer) {
        free(type);
        return STARK_MEMORY_ERROR;
    }

    size_t size = type->size;
    stark_result_t result = stark_get_str(db, data_key, buffer, &size);

    if (result == STARK_OK) {
        printf("Data retrieved, size: %zu bytes\n", size);
        result = type_deserialize(type->fields, type->field_count,
                                  buffer, output, output_size);
    } else if (result == STARK_NOT_FOUND) {
        printf("Data key '%s' not found\n", data_key);
    }

    free(buffer);
    free(type);
    return result;
}

// ==================== TRANSACTIONS ====================

typedef struct {
    uint32_t key;
    void* old_value;
    size_t old_size;
    void* new_value;
    size_t new_size;
    int is_delete;
} TransactionEntry;

STARK_API stark_result_t stark_begin(stark_db_t* db) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (db->in_transaction) return STARK_ERROR;
    db->in_transaction = 1;
    db->transaction_log = malloc(1024);
    db->log_size = 0;
    if (!db->transaction_log) {
        db->in_transaction = 0;
        return STARK_MEMORY_ERROR;
    }
    printf("Transaction started\n");
    return STARK_OK;
}

STARK_API stark_result_t stark_commit(stark_db_t* db) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (!db->in_transaction) return STARK_ERROR;
    free(db->transaction_log);
    db->transaction_log = NULL;
    db->log_size = 0;
    db->in_transaction = 0;
    printf("Transaction committed\n");
    return STARK_OK;
}

STARK_API stark_result_t stark_rollback(stark_db_t* db) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (!db->in_transaction) return STARK_ERROR;
    free(db->transaction_log);
    db->transaction_log = NULL;
    db->log_size = 0;
    db->in_transaction = 0;
    printf("Transaction rolled back\n");
    return STARK_OK;
}

STARK_API int stark_in_transaction(stark_db_t* db) {
    if (!db) return 0;
    return db->in_transaction;
}
