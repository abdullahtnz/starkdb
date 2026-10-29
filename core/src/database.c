#include "database.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

static uint32_t hash_string(const char* str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

Database *db_open(const char *db_name) {
    return db_open_mode(db_name, 0);
}

Database *db_open_mode(const char *db_name, int readonly) {
    Database *db = malloc(sizeof(Database));
    if (!db) return NULL;

    db->name = strdup(db_name);
    db->total_keys = 0;
    db->total_data_size = 0;

    char index_filename[256];
    char data_filename[256];
    snprintf(index_filename, sizeof(index_filename), "%s.idx", db_name);
    snprintf(data_filename, sizeof(data_filename), "%s.dat", db_name);

    Pager *index_pager = pager_open_mode(index_filename, readonly);
    if (!index_pager) {
        free(db->name);
        free(db);
        return NULL;
    }

    Pager *data_pager = pager_open_mode(data_filename, readonly);
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

// Rebuild the in-memory B-tree root and storage cursor from the (already
// truncated / reverted) on-disk state, keeping the same open pagers.
DB_Result db_reinit(Database *db) {
    if (!db || !db->index || !db->storage) return DB_ERROR;

    Pager *index_pager = db->index->pager;
    Pager *data_pager = db->storage->pager;

    free(db->index);
    free(db->storage);
    db->index = NULL;
    db->storage = NULL;

    db->index = btree_create(index_pager);
    if (!db->index) return DB_ERROR;
    db->storage = storage_create(data_pager);
    if (!db->storage) return DB_ERROR;

    db->total_keys = 0;
    db->total_data_size = 0;
    return DB_SUCCESS;
}

DB_Result db_insert(Database *db, uint32_t key, const void *data, size_t size) {
    data_addr_t addr;
    DB_Result result = storage_write(db->storage, data, size, &addr);
    if (result != DB_SUCCESS) {
        return result;
    }

    result = btree_insert(db->index, key, addr);
    if (result != DB_SUCCESS) {
    }

    db->total_keys++;
    db->total_data_size += size;
    return result;
}

DB_Result db_find(Database *db, uint32_t key, void *buffer, size_t *size) {
    uint64_t addr;
    DB_Result result = btree_find(db->index, key, &addr);
    if (result != DB_SUCCESS) {
        return result;
    }

    result = storage_read(db->storage, addr, buffer, size);

    return result;
}

DB_Result db_delete(Database *db, uint32_t key) {
    if (!db || !db->index) return DB_ERROR;

    uint64_t addr;
    DB_Result result = btree_find(db->index, key, &addr);

    if (result == DB_SUCCESS) {
        storage_delete(db->storage, addr);
    }

    result = btree_delete(db->index, key);

    if (result == DB_SUCCESS) {
        if (db->total_keys > 0) db->total_keys--;
    }

    return result;
}

// Binary array on-disk layout.
//
// New format (always written by db_insert_binary):
//   [uint32 magic=0x53544259][uint8 dtype][uint8 reserved]
//   [uint16 header_version=1][size_t elem_size][size_t count][raw data]
//
// Legacy format (still readable, inferred from element size):
//   [size_t elem_size][size_t count][raw data]
#define BINARY_MAGIC 0x53544259u
#define BINARY_HEADER_VERSION 1u
#define BINARY_HEADER_SIZE (sizeof(uint32_t) + 1 + 1 + sizeof(uint16_t) + sizeof(size_t) * 2)

static uint8_t infer_dtype_from_elem_size(size_t elem_size) {
    switch (elem_size) {
        case 4: return 0;  // FLOAT32
        case 8: return 1;  // FLOAT64
        case 2: return 6;  // INT16
        case 1: return 4;  // UINT8
        default: return 2; // INT32
    }
}

// Locate a binary value and parse its header. Returns a pointer to the raw
// element data via *data.
static DB_Result binary_read_header(Database *db, uint32_t key,
                                    const uint8_t **data, size_t *elem_size,
                                    size_t *count, uint8_t *dtype,
                                    uint16_t *bad_version) {
    uint64_t addr;
    DB_Result result = btree_find(db->index, key, &addr);
    if (result != DB_SUCCESS) return result;

    size_t size = 0;
    void *raw = storage_get_ptr(db->storage, addr, &size);
    if (!raw || size < sizeof(uint32_t)) return DB_ERROR;

    const uint8_t *p = (const uint8_t *)raw;
    uint32_t magic;
    memcpy(&magic, p, sizeof(uint32_t));

    if (magic == BINARY_MAGIC) {
        if (size < BINARY_HEADER_SIZE) return DB_ERROR;
        uint8_t dt = p[4];
        uint16_t version;
        memcpy(&version, p + 6, sizeof(uint16_t));
        if (version != BINARY_HEADER_VERSION) {
            if (bad_version) *bad_version = version;
            return DB_ERROR;
        }
        size_t es, cnt;
        memcpy(&es, p + 8, sizeof(size_t));
        memcpy(&cnt, p + 8 + sizeof(size_t), sizeof(size_t));
        if (es != 0 && cnt > (size - BINARY_HEADER_SIZE) / es) return DB_ERROR;

        *elem_size = es;
        *count = cnt;
        *dtype = dt;
        *data = p + BINARY_HEADER_SIZE;
    } else {
        // Legacy format.
        if (size < sizeof(size_t) * 2) return DB_ERROR;
        size_t es, cnt;
        memcpy(&es, p, sizeof(size_t));
        memcpy(&cnt, p + sizeof(size_t), sizeof(size_t));
        if (es != 0 && cnt > (size - sizeof(size_t) * 2) / es) return DB_ERROR;

        *elem_size = es;
        *count = cnt;
        *dtype = infer_dtype_from_elem_size(es);
        *data = p + sizeof(size_t) * 2;
    }

    return DB_SUCCESS;
}

DB_Result db_insert_binary(Database *db, uint32_t key, uint8_t dtype,
                            const void *data, size_t elem_size, size_t count,
                            data_addr_t *addr) {
    if (!db || elem_size == 0) return DB_ERROR;
    if (count > 0 && !data) return DB_ERROR;

    size_t data_bytes = elem_size * count;
    size_t total_size = BINARY_HEADER_SIZE + data_bytes;
    void *packed = malloc(total_size);
    if (!packed) return DB_MEMORY_ERROR;

    uint8_t *p = (uint8_t *)packed;
    uint32_t magic = BINARY_MAGIC;
    memcpy(p, &magic, sizeof(uint32_t)); p += sizeof(uint32_t);
    *p++ = dtype;
    *p++ = 0;  // reserved
    uint16_t version = BINARY_HEADER_VERSION;
    memcpy(p, &version, sizeof(uint16_t)); p += sizeof(uint16_t);
    memcpy(p, &elem_size, sizeof(size_t)); p += sizeof(size_t);
    memcpy(p, &count, sizeof(size_t)); p += sizeof(size_t);
    if (data_bytes > 0) memcpy(p, data, data_bytes);

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
                          size_t *count, size_t *elem_size, uint8_t *dtype,
                          uint16_t *bad_version) {
    if (!db || !count || !elem_size) return DB_ERROR;
    if (bad_version) *bad_version = 0;

    const uint8_t *data;
    size_t es, cnt;
    uint8_t dt;
    DB_Result result = binary_read_header(db, key, &data, &es, &cnt, &dt, bad_version);
    if (result != DB_SUCCESS) return result;

    size_t capacity = *count;
    *elem_size = es;
    *count = cnt;
    if (dtype) *dtype = dt;
    if (es * cnt > 0) {
        if (!buffer || capacity < cnt) return DB_BUFFER_TOO_SMALL;
        memcpy(buffer, data, es * cnt);
    }
    return DB_SUCCESS;
}

DB_Result db_get_binary_ptr(Database *db, uint32_t key, void **ptr,
                             size_t *count, size_t *elem_size, uint8_t *dtype,
                             uint16_t *bad_version) {
    if (!db || !ptr) return DB_ERROR;
    if (bad_version) *bad_version = 0;

    const uint8_t *data;
    size_t es, cnt;
    uint8_t dt;
    DB_Result result = binary_read_header(db, key, &data, &es, &cnt, &dt, bad_version);
    if (result != DB_SUCCESS) return result;

    if (elem_size) *elem_size = es;
    if (count) *count = cnt;
    if (dtype) *dtype = dt;
    *ptr = (void *)data;
    return DB_SUCCESS;
}

DB_Result db_store_columns(Database *db, const char *col_key, const char *schema,
                            void **columns, size_t *col_sizes, size_t num_cols,
                            size_t num_rows) {
    if (!db || !col_key || !schema || !columns || !col_sizes) return DB_ERROR;

    size_t schema_len = strlen(schema) + 1;
    size_t header_size = sizeof(uint32_t) + sizeof(size_t) * 2 + schema_len + sizeof(size_t) * num_cols;

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

// ==================== COMPACTION ====================

#define COMPACT_MAX_KEYS 10000000u

typedef struct { uint32_t key; data_addr_t addr; } CompactEntry;

static void fsync_parent_dir(const char* path) {
    char dir[1024];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char* slash = strrchr(dir, '/');
    if (slash) *slash = '\0';
    else strcpy(dir, ".");
    int fd = open(dir, O_RDONLY | O_DIRECTORY);
    if (fd >= 0) { fsync(fd); close(fd); }
}

DB_Result db_compact(Database *db, int *out_lock_fd) {
    if (out_lock_fd) *out_lock_fd = -1;
    if (!db || !db->index || !db->storage) return DB_ERROR;

    char idx_path[1024], dat_path[1024];
    char idx_new[1024], dat_new[1024];
    char idx_old[1024], dat_old[1024];
    char sentinel[1024];
    snprintf(idx_path, sizeof(idx_path), "%s.idx", db->name);
    snprintf(dat_path, sizeof(dat_path), "%s.dat", db->name);
    snprintf(idx_new, sizeof(idx_new), "%s.idx.new", db->name);
    snprintf(dat_new, sizeof(dat_new), "%s.dat.new", db->name);
    snprintf(idx_old, sizeof(idx_old), "%s.idx.old", db->name);
    snprintf(dat_old, sizeof(dat_old), "%s.dat.old", db->name);
    snprintf(sentinel, sizeof(sentinel), "%s.compact", db->name);

    // Flush the live state so the originals are durable before we begin.
    pager_flush_all(db->index->pager);
    pager_flush_all(db->storage->pager);
    pager_fsync(db->index->pager);
    pager_fsync(db->storage->pager);

    // Discard any stale temp files from a previously aborted attempt.
    remove(dat_new);
    remove(idx_new);

    // ---- Phase 1: enumerate live (key, addr) pairs ----
    uint32_t capacity = 256;
    uint32_t count = 0;
    CompactEntry* entries = malloc((size_t)capacity * sizeof(CompactEntry));
    if (!entries) return DB_MEMORY_ERROR;

    BTreeCursor* cursor = btree_cursor_create(db->index);
    if (!cursor) { free(entries); return DB_ERROR; }

    DB_Result r = btree_cursor_first(cursor);
    while (r == DB_SUCCESS) {
        uint32_t key;
        uint64_t addr;
        if (btree_cursor_get(cursor, &key, &addr) != DB_SUCCESS) {
            btree_cursor_destroy(cursor);
            free(entries);
            return DB_ERROR;
        }
        if (count >= COMPACT_MAX_KEYS) {
            btree_cursor_destroy(cursor);
            free(entries);
            return DB_FULL;
        }
        if (count == capacity) {
            uint32_t newcap = capacity * 2;
            if (newcap > COMPACT_MAX_KEYS) newcap = COMPACT_MAX_KEYS;
            CompactEntry* ne = realloc(entries, (size_t)newcap * sizeof(CompactEntry));
            if (!ne) { btree_cursor_destroy(cursor); free(entries); return DB_MEMORY_ERROR; }
            entries = ne;
            capacity = newcap;
        }
        entries[count].key = key;
        entries[count].addr = (data_addr_t)addr;
        count++;
        r = btree_cursor_next(cursor);
    }
    btree_cursor_destroy(cursor);
    if (r != DB_NOT_FOUND) { free(entries); return DB_ERROR; }

    // ---- Phase 1b: write live values to <base>.dat.new ----
    Pager* ndp = pager_open(dat_new);
    if (!ndp) { free(entries); return DB_IO_ERROR; }
    Storage* ns = storage_create(ndp);
    if (!ns) { pager_close(ndp); free(entries); return DB_ERROR; }

    for (uint32_t i = 0; i < count; i++) {
        size_t size = 0;
        void* ptr = storage_get_ptr(db->storage, entries[i].addr, &size);
        if (!ptr) { free(ns); pager_close(ndp); free(entries); return DB_ERROR; }
        data_addr_t new_addr;
        DB_Result wr = storage_write(ns, ptr, size, &new_addr);
        if (wr != DB_SUCCESS) { free(ns); pager_close(ndp); free(entries); return wr; }
        entries[i].addr = new_addr;
    }

    pager_flush_all(ndp);
    pager_fsync(ndp);
    pager_close(ndp);
    free(ns);

    // ---- Phase 2: rebuild the index into <base>.idx.new ----
    Pager* nip = pager_open(idx_new);
    if (!nip) { free(entries); return DB_IO_ERROR; }
    BTree* nt = btree_create(nip);
    if (!nt) { pager_close(nip); free(entries); return DB_ERROR; }

    for (uint32_t i = 0; i < count; i++) {
        DB_Result ir = btree_insert(nt, entries[i].key, entries[i].addr);
        if (ir != DB_SUCCESS) { free(nt); pager_close(nip); free(entries); return ir; }
    }

    pager_flush_all(nip);
    pager_fsync(nip);
    pager_close(nip);
    free(nt);
    free(entries);

    // ---- Phase 3: crash-safe swap ----
    // Lock the newly built index before it is installed.  flock() is attached
    // to the inode, so the lock survives the rename to <base>.idx and the
    // caller can hand over ownership without an unlocked window.
    int new_lock = -1;
    if (pager_acquire_lock(idx_new, 0, 0, &new_lock) != 0) {
        return DB_IO_ERROR;
    }

    FILE* sf = fopen(sentinel, "wb");
    if (!sf) {
        pager_release_lock(new_lock);
        return DB_IO_ERROR;
    }
    fprintf(sf, "%s\n", COMPACT_SENTINEL_MAGIC);
    fflush(sf);
    if (fsync(fileno(sf)) != 0) {
        fclose(sf);
        pager_release_lock(new_lock);
        return DB_IO_ERROR;
    }
    fclose(sf);

    if (rename(dat_path, dat_old) != 0) { pager_release_lock(new_lock); return DB_IO_ERROR; }
    if (rename(idx_path, idx_old) != 0) { pager_release_lock(new_lock); return DB_IO_ERROR; }
    if (rename(dat_new, dat_path) != 0) { pager_release_lock(new_lock); return DB_IO_ERROR; }
    if (rename(idx_new, idx_path) != 0) { pager_release_lock(new_lock); return DB_IO_ERROR; }
    fsync_parent_dir(dat_path);

    // Close pagers still attached to the old inodes (now named *.old), then
    // drop backups and reopen the freshly installed files.
    pager_close(db->index->pager);
    pager_close(db->storage->pager);
    free(db->index);
    free(db->storage);
    db->index = NULL;
    db->storage = NULL;

    remove(dat_old);
    remove(idx_old);
    remove(sentinel);

    Pager* ip = pager_open(idx_path);
    Pager* dp = pager_open(dat_path);
    if (!ip || !dp) {
        if (ip) pager_close(ip);
        if (dp) pager_close(dp);
        pager_release_lock(new_lock);
        return DB_IO_ERROR;
    }
    db->index = btree_create(ip);
    db->storage = storage_create(dp);
    if (!db->index || !db->storage) {
        pager_release_lock(new_lock);
        return DB_ERROR;
    }
    db->total_keys = 0;
    db->total_data_size = 0;
    if (out_lock_fd) *out_lock_fd = new_lock;
    else pager_release_lock(new_lock);
    return DB_SUCCESS;
}
