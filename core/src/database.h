#ifndef DATABASE_H
#define DATABASE_H

#include "constants.h"
#include "btree.h"
#include "storage.h"

typedef struct { uint32_t size; uint8_t data[]; } DataBlock;

typedef struct Database {
    BTree *index;
    Storage *storage;
    char *name;
    uint64_t total_keys;
    uint64_t total_data_size;
} Database;

Database *db_open(const char *db_name);
Database *db_open_mode(const char *db_name, int readonly);
DB_Result db_close(Database *db);
DB_Result db_reinit(Database *db);
DB_Result db_insert(Database *db, uint32_t key, const void *data, size_t size);
DB_Result db_find(Database *db, uint32_t key, void *buffer, size_t *size);
DB_Result db_delete(Database *db, uint32_t key);
DB_Result db_insert_binary(Database *db, uint32_t key, uint8_t dtype, const void *data, size_t elem_size, size_t count, data_addr_t *addr);
DB_Result db_find_binary(Database *db, uint32_t key, void *buffer, size_t *count, size_t *elem_size, uint8_t *dtype, uint16_t *bad_version);
DB_Result db_get_binary_ptr(Database *db, uint32_t key, void **ptr, size_t *count, size_t *elem_size, uint8_t *dtype, uint16_t *bad_version);
DB_Result db_store_columns(Database *db, const char *col_key, const char *schema, void **columns, size_t *col_sizes, size_t num_cols, size_t num_rows);
DB_Result db_load_columns(Database *db, const char *col_key, char **schema_out, void ***columns, size_t **col_sizes, size_t *num_cols, size_t *num_rows);
DataBlock *db_get_data_block(Database *db, data_addr_t addr);

// Rewrite .dat with only live values and rebuild .idx (crash-safe).
// On success *out_lock_fd (when non-NULL) receives a lock descriptor for the
// newly installed .idx file, so the caller can hand the DB lock over without a
// window in which another process could acquire it.  -1 means no lock.
DB_Result db_compact(Database *db, int *out_lock_fd);

#endif
