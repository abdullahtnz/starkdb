#include "urage.h"
#include "database.h"  // Your existing internal header
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct urage_db {
    Database* internal_db;
    char last_error[256];
    char* path;
};

// ==================== LIFECYCLE ====================

URAGE_API urage_db_t* urage_open(const char* path, unsigned flags) {
    (void)flags;  // Unused for now
    
    urage_db_t* db = (urage_db_t*)calloc(1, sizeof(urage_db_t));
    if (!db) return NULL;
    
    db->path = strdup(path);
    
    // Call your existing database code
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

URAGE_API void urage_close(urage_db_t* db) {
    if (!db) return;
    
    if (db->internal_db) {
        db_close(db->internal_db);
    }
    
    free(db->path);
    free(db);
}

// ==================== CRUD ====================

URAGE_API urage_result_t urage_add(urage_db_t* db, uint32_t key,
                                   const void* value, size_t value_size) {
    if (!db || !db->internal_db) return URAGE_CLOSED;
    
    DB_Result result = db_insert(db->internal_db, key, value, value_size);
    
    switch (result) {
        case DB_SUCCESS: return URAGE_OK;
        case DB_FULL: return URAGE_FULL;
        case DB_IO_ERROR: return URAGE_IO_ERROR;
        case DB_MEMORY_ERROR: return URAGE_ERROR;
        default: return URAGE_ERROR;
    }
}

URAGE_API urage_result_t urage_get(urage_db_t* db, uint32_t key,
                                   void* buffer, size_t* buffer_size) {
    if (!db || !db->internal_db) return URAGE_CLOSED;
    if (!buffer || !buffer_size) return URAGE_INVALID_ARG;
    
    DB_Result result = db_find(db->internal_db, key, buffer, buffer_size);
    
    switch (result) {
        case DB_SUCCESS: return URAGE_OK;
        case DB_NOT_FOUND: return URAGE_NOT_FOUND;
        case DB_ERROR: return URAGE_ERROR;
        default: return URAGE_ERROR;
    }
}

URAGE_API urage_result_t urage_delete(urage_db_t* db, uint32_t key) {
    if (!db || !db->internal_db) return URAGE_CLOSED;
    
    DB_Result result = db_delete(db->internal_db, key);
    
    switch (result) {
        case DB_SUCCESS: return URAGE_OK;
        case DB_NOT_FOUND: return URAGE_NOT_FOUND;
        default: return URAGE_ERROR;
    }
}

URAGE_API int urage_exists(urage_db_t* db, uint32_t key) {
    if (!db || !db->internal_db) return 0;
    
    char dummy[256];  // ← Make it larger
    size_t size = sizeof(dummy);
    DB_Result result = db_find(db->internal_db, key, dummy, &size);
    
    // If it returns DB_SUCCESS, key exists
    // If it returns DB_NOT_FOUND, key doesn't exist
    // If it returns DB_ERROR but size > 0, key exists but buffer too small
    return (result == DB_SUCCESS) || (result == DB_ERROR && size > 0);
}

// ==================== CURSOR ====================

struct urage_cursor {
    urage_db_t* db;
    // TODO: Add cursor state when implemented in core
    uint32_t current_key;
    int valid;
};

URAGE_API urage_cursor_t* urage_cursor_create(urage_db_t* db) {
    if (!db) return NULL;
    
    urage_cursor_t* cursor = (urage_cursor_t*)calloc(1, sizeof(urage_cursor_t));
    if (!cursor) return NULL;
    
    cursor->db = db;
    cursor->valid = 0;
    cursor->current_key = 0;
    
    // TODO: Initialize cursor when core supports iteration
    // For now, just return a placeholder
    
    return cursor;
}

URAGE_API urage_result_t urage_cursor_first(urage_cursor_t* cursor) {
    if (!cursor) return URAGE_INVALID_ARG;
    // TODO: Implement when core supports iteration
    cursor->valid = 0;
    return URAGE_NOT_FOUND;
}

URAGE_API urage_result_t urage_cursor_last(urage_cursor_t* cursor) {
    if (!cursor) return URAGE_INVALID_ARG;
    // TODO: Implement when core supports iteration
    cursor->valid = 0;
    return URAGE_NOT_FOUND;
}

URAGE_API urage_result_t urage_cursor_next(urage_cursor_t* cursor) {
    if (!cursor) return URAGE_INVALID_ARG;
    // TODO: Implement when core supports iteration
    return URAGE_NOT_FOUND;
}

URAGE_API urage_result_t urage_cursor_prev(urage_cursor_t* cursor) {
    if (!cursor) return URAGE_INVALID_ARG;
    // TODO: Implement when core supports iteration
    return URAGE_NOT_FOUND;
}

URAGE_API urage_result_t urage_cursor_get(urage_cursor_t* cursor,
                                         uint32_t* key,
                                         void* buffer, size_t* buffer_size) {
    if (!cursor || !cursor->valid) return URAGE_NOT_FOUND;
    if (!key || !buffer || !buffer_size) return URAGE_INVALID_ARG;
    
    // TODO: Implement when core supports iteration
    return URAGE_NOT_FOUND;
}

URAGE_API void urage_cursor_destroy(urage_cursor_t* cursor) {
    free(cursor);
}

// ==================== STATISTICS ====================

URAGE_API urage_result_t urage_stats(urage_db_t* db, urage_stats_t* stats) {
    if (!db || !db->internal_db) return URAGE_CLOSED;
    if (!stats) return URAGE_INVALID_ARG;
    
    Database* internal = db->internal_db;
    
    // Get page count from pager
    stats->page_count = internal->storage->pager->num_pages;
    
    // Estimate keys from B-tree root
    // This is a hack - you need proper counting
    void* root = pager_get_page(internal->index->pager, internal->index->root_page_num);
    NodeHeader* header = (NodeHeader*)root;
    
    if (header->type == NODE_LEAF) {
        LeafNode* leaf = (LeafNode*)root;
        stats->keys_count = leaf->num_cells;
    } else {
        stats->keys_count = 0;  // Would need to traverse
    }
    
    stats->btree_height = 1;  // Assume height 1
    stats->data_size = internal->storage->next_offset;
    
    return URAGE_OK;
}

URAGE_API const char* urage_error(urage_db_t* db) {
    if (!db) return "Database handle is NULL";
    return db->last_error;
}

URAGE_API urage_result_t urage_sync(urage_db_t* db) {
    if (!db || !db->internal_db) return URAGE_CLOSED;
    
    // Flush all changes to disk using your existing pager
    Database* internal = db->internal_db;
    pager_flush_all(internal->index->pager);
    pager_flush_all(internal->storage->pager);
    
    return URAGE_OK;
}