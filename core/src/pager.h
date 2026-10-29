#ifndef PAGER_H
#define PAGER_H

#include "constants.h"
#include <stdio.h>

typedef struct Pager {
    FILE *file;
    void *pages[TABLE_MAX_PAGES];  // Page cache
    page_num_t num_pages;
    uint32_t page_size;
    uint32_t valid_version;        // file format version read from header
    int flush_suppressed;          // when set, flushes are no-ops (transactions)
    int readonly;                  // when set, the file is opened read-only
} Pager;

// Initialize and destroy
Pager *pager_open(const char *filename);
Pager *pager_open_mode(const char *filename, int readonly);
void pager_close(Pager *pager);

// Page operations
void *pager_get_page(Pager *pager, page_num_t page_num);
DB_Result pager_flush_page(Pager *pager, page_num_t page_num);
DB_Result pager_flush_all(Pager *pager);
page_num_t pager_allocate_page(Pager *pager);

// Transaction / compaction helpers
uint64_t pager_file_size(Pager *pager);
DB_Result pager_truncate(Pager *pager, uint64_t size);
DB_Result pager_fsync(Pager *pager);
void pager_invalidate_all(Pager *pager);
void pager_set_flush_suppressed(Pager *pager, int suppressed);

// Cross-process file locking.
//
// The lock is acquired on the .idx file before any file modification.  .idx is
// always the first component opened and always exists for a valid database, so
// locking it serializes access to the whole database (both files).
//
// Returns 0 on success and stores the descriptor in *out_fd.  On failure
// returns -1 and leaves errno set by open()/flock().  On platforms without a
// locking implementation the function succeeds with *out_fd == -1.
int pager_acquire_lock(const char *filename, int create, int shared, int *out_fd);
void pager_release_lock(int fd);

// File offset of a page, accounting for the 16-byte file header.
static inline uint64_t page_file_offset(page_num_t page_num) {
    return (uint64_t)HEADER_SIZE + (uint64_t)page_num * (uint64_t)PAGE_SIZE;
}

#endif
