#include "database.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static uint32_t hash_string(const char* str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

Database *db_open(const char *db_name) {
    Database *db = malloc(sizeof(Database));
    if (!db) return NULL;

    db->name = strdup(db_name);
    db->total_keys = 0;
    db->total_data_size = 0;

    char index_filename[256];
    char data_filename[256];
    snprintf(index_filename, sizeof(index_filename), "%s.idx", db_name);
    snprintf(data_filename, sizeof(data_filename), "%s.dat", db_name);

    Pager *index_pager = pager_open(index_filename);
    if (!index_pager) {
        free(db->name);
        free(db);
        return NULL;
    }

    Pager *data_pager = pager_open(data_filename);
    if (!data_pager) {
        pager_close(index_pager);
        free(db->name);
        free(db);
        return NULL;
    }

    db->index = btree_create(index_pager);
    if (!db->index) {
        pager_close(index_pager);
        pager_close(data_pager);
        free(db->name);
        free(db);
        return NULL;
    }

    db->storage = storage_create(data_pager);
    if (!db->storage) {
        free(db->index);
        pager_close(index_pager);
        pager_close(data_pager);
        free(db->name);
        free(db);
        return NULL;
    }

    return db;
}

DB_Result db_close(Database *db) {
    if (!db) return DB_ERROR;

    pager_flush_all(db->index->pager);
    pager_flush_all(db->storage->pager);

    pager_close(db->index->pager);
    pager_close(db->storage->pager);

    free(db->index);
    free(db->storage);
    free(db->name);
    free(db);

    return DB_SUCCESS;
}

DB_Result db_insert(Database *db, uint32_t key, const void *data, size_t size) {
    printf("Debug: Inserting key %u with data size %zu\n", key, size);

    data_addr_t addr;
    DB_Result result = storage_write(db->storage, data, size, &addr);
    if (result != DB_SUCCESS) {
        printf("Debug: storage_write failed with code %d\n", result);
        return result;
    }

    printf("Debug: Stored at addr 0x%016lX\n", (unsigned long)addr);

    result = btree_insert(db->index, key, addr);
    if (result != DB_SUCCESS) {
        printf("Debug: btree_insert failed with code %d\n", result);
    }

    db->total_keys++;
    db->total_data_size += size;
    return result;
}

DB_Result db_find(Database *db, uint32_t key, void *buffer, size_t *size) {
    printf("Debug: db_find(key=%u)\n", key);

    uint64_t addr;
    DB_Result result = btree_find(db->index, key, &addr);
    if (result != DB_SUCCESS) {
        printf("Debug: btree_find failed with code %d\n", result);
        return result;
    }

    printf("Debug: btree_find returned addr=0x%016lX\n", (unsigned long)addr);

    result = storage_read(db->storage, addr, buffer, size);
    printf("Debug: storage_read returned %d\n", result);

    return result;
}

DB_Result db_delete(Database *db, uint32_t key) {
    if (!db || !db->index) return DB_ERROR;

    printf("Debug: db_delete(key=%u)\n", key);

    uint64_t addr;
    DB_Result result = btree_find(db->index, key, &addr);

    if (result == DB_SUCCESS) {
        storage_delete(db->storage, addr);
        printf("Debug: Deleted storage at addr 0x%016lX\n", (unsigned long)addr);
    }

    result = btree_delete(db->index, key);

    if (result == DB_SUCCESS) {
        printf("Deleted key %u\n", key);
        if (db->total_keys > 0) db->total_keys--;
    }

    return result;
}

DB_Result db_insert_binary(Database *db, uint32_t key, const void *data,
                            size_t elem_size, size_t count, data_addr_t *addr) {
    if (!db || !data || elem_size == 0 || count == 0) return DB_ERROR;

    size_t total_size = sizeof(size_t) * 2 + elem_size * count;
    void *packed = malloc(total_size);
    if (!packed) return DB_MEMORY_ERROR;

    memcpy(packed, &elem_size, sizeof(size_t));
    memcpy((char *)packed + sizeof(size_t), &count, sizeof(size_t));
    memcpy((char *)packed + sizeof(size_t) * 2, data, elem_size * count);

    DB_Result result = db_insert(db, key, packed, total_size);
    if (result == DB_SUCCESS && addr) {
        uint64_t stored_addr;
        btree_find(db->index, key, &stored_addr);
        *addr = stored_addr;
    }

    free(packed);
    return result;
}

DB_Result db_find_binary(Database *db, uint32_t key, void *buffer,
                          size_t *count, size_t *elem_size) {
    if (!db || !buffer || !count || !elem_size) return DB_ERROR;

    uint64_t addr;
    DB_Result result = btree_find(db->index, key, &addr);
    if (result != DB_SUCCESS) return result;

    void *raw = storage_get_ptr(db->storage, addr, NULL);
    if (!raw) return DB_ERROR;

    size_t es, cnt;
    memcpy(&es, raw, sizeof(size_t));
    memcpy(&cnt, (char *)raw + sizeof(size_t), sizeof(size_t));

    *elem_size = es;
    *count = cnt;

    memcpy(buffer, (char *)raw + sizeof(size_t) * 2, es * cnt);
    return DB_SUCCESS;
}

DB_Result db_get_binary_ptr(Database *db, uint32_t key, void **ptr,
                             size_t *count, size_t *elem_size) {
    if (!db || !ptr) return DB_ERROR;

    uint64_t addr;
    DB_Result result = btree_find(db->index, key, &addr);
    if (result != DB_SUCCESS) return result;

    void *raw = storage_get_ptr(db->storage, addr, NULL);
    if (!raw) return DB_ERROR;

    if (elem_size) memcpy(elem_size, raw, sizeof(size_t));
    if (count) memcpy(count, (char *)raw + sizeof(size_t), sizeof(size_t));

    *ptr = (char *)raw + sizeof(size_t) * 2;
    return DB_SUCCESS;
}

DB_Result db_store_columns(Database *db, const char *col_key, const char *schema,
                            void **columns, size_t *col_sizes, size_t num_cols,
                            size_t num_rows) {
    if (!db || !col_key || !schema || !columns || !col_sizes) return DB_ERROR;

    size_t schema_len = strlen(schema) + 1;
    size_t header_size = sizeof(uint32_t) + sizeof(size_t) + schema_len + sizeof(size_t) * num_cols;

    uint32_t magic = 0x4152524F; /* "ARRO" */
    uint8_t *packed = malloc(header_size);
    if (!packed) return DB_MEMORY_ERROR;

    uint8_t *p = packed;
    memcpy(p, &magic, sizeof(uint32_t)); p += sizeof(uint32_t);
    memcpy(p, &num_cols, sizeof(size_t)); p += sizeof(size_t);
    memcpy(p, &num_rows, sizeof(size_t)); p += sizeof(size_t);
    memcpy(p, schema, schema_len); p += schema_len;
    for (size_t i = 0; i < num_cols; i++) {
        memcpy(p, &col_sizes[i], sizeof(size_t));
        p += sizeof(size_t);
    }

    char schema_key[384];
    snprintf(schema_key, sizeof(schema_key), "%s__schema", col_key);
    DB_Result result = db_insert(db, hash_string(col_key), packed, header_size);
    free(packed);
    if (result != DB_SUCCESS) return result;

    for (size_t i = 0; i < num_cols; i++) {
        char col_data_key[384];
        snprintf(col_data_key, sizeof(col_data_key), "%s__col%zu", col_key, i);
        uint32_t hk = hash_string(col_data_key);
        result = db_insert(db, hk, columns[i], col_sizes[i]);
        if (result != DB_SUCCESS) return result;
    }

    return DB_SUCCESS;
}

DB_Result db_load_columns(Database *db, const char *col_key, char **schema_out,
                           void ***columns, size_t **col_sizes, size_t *num_cols,
                           size_t *num_rows) {
    if (!db || !col_key) return DB_ERROR;

    char schema_key[384];
    snprintf(schema_key, sizeof(schema_key), "%s__schema", col_key);
    uint32_t sk = hash_string(col_key);

    uint64_t addr;
    DB_Result result = btree_find(db->index, sk, &addr);
    if (result != DB_SUCCESS) return result;

    void *raw = storage_get_ptr(db->storage, addr, NULL);
    if (!raw) return DB_ERROR;

    uint8_t *p = (uint8_t *)raw;
    uint32_t magic;
    memcpy(&magic, p, sizeof(uint32_t));
    if (magic != 0x4152524F) return DB_ERROR;
    p += sizeof(uint32_t);

    size_t nc, nr;
    memcpy(&nc, p, sizeof(size_t)); p += sizeof(size_t);
    memcpy(&nr, p, sizeof(size_t)); p += sizeof(size_t);

    if (schema_out) {
        *schema_out = strdup((char *)p);
    }
    p += strlen((char *)p) + 1;

    if (col_sizes) {
        *col_sizes = malloc(nc * sizeof(size_t));
    }
    if (columns) {
        *columns = malloc(nc * sizeof(void *));
    }

    for (size_t i = 0; i < nc; i++) {
        size_t col_size;
        memcpy(&col_size, p, sizeof(size_t));
        p += sizeof(size_t);
        if (col_sizes) (*col_sizes)[i] = col_size;

        char col_data_key[384];
        snprintf(col_data_key, sizeof(col_data_key), "%s__col%zu", col_key, i);
        uint32_t ck = hash_string(col_data_key);

        uint64_t col_addr;
        if (btree_find(db->index, ck, &col_addr) == DB_SUCCESS) {
            void *coldata = storage_get_ptr(db->storage, col_addr, NULL);
            if (columns) (*columns)[i] = coldata;
        } else {
            if (columns) (*columns)[i] = NULL;
        }
    }

    if (num_cols) *num_cols = nc;
    if (num_rows) *num_rows = nr;

    return DB_SUCCESS;
}

DataBlock *db_get_data_block(Database *db, data_addr_t addr) {
    if (!db) return NULL;
    size_t size;
    void *ptr = storage_get_ptr(db->storage, addr, &size);
    if (!ptr) return NULL;
    DataBlock *block = malloc(sizeof(DataBlock) + size);
    if (!block) return NULL;
    block->size = (uint32_t)size;
    memcpy(block->data, ptr, size);
    return block;
}
