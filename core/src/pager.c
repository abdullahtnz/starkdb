#include "pager.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#endif

// Write the 16-byte file header at offset 0 and fsync it.
static int pager_write_header(Pager *pager) {
    uint8_t header[HEADER_SIZE];
    memcpy(header, STARKDB_MAGIC, STARKDB_MAGIC_LEN);
    uint32_t version = STARKDB_VERSION;
    uint32_t flags = 0;
    memcpy(header + 8, &version, sizeof(uint32_t));
    memcpy(header + 12, &flags, sizeof(uint32_t));

    if (fseek(pager->file, 0, SEEK_SET) != 0) return -1;
    if (fwrite(header, 1, HEADER_SIZE, pager->file) != HEADER_SIZE) return -1;
    if (fflush(pager->file) != 0) return -1;
    if (fsync(fileno(pager->file)) != 0) return -1;
    return 0;
}

// Read and validate the header. Returns 0 on success, -1 on invalid header.
static int pager_read_header(Pager *pager, const char *filename) {
    uint8_t header[HEADER_SIZE];
    if (fseek(pager->file, 0, SEEK_SET) != 0) return -1;
    if (fread(header, 1, HEADER_SIZE, pager->file) != HEADER_SIZE) return -1;

    if (memcmp(header, STARKDB_MAGIC, STARKDB_MAGIC_LEN) != 0) {
        fprintf(stderr,
                "STARKDB: '%s' is not a STARKDB file (bad magic). "
                "If this is an old-format database, run stark_upgrade_file() first.\n",
                filename);
        return -1;
    }

    uint32_t version;
    memcpy(&version, header + 8, sizeof(uint32_t));
    if (version > STARKDB_VERSION) {
        fprintf(stderr,
                "STARKDB: file '%s' version %u is newer than supported version %u\n",
                filename, version, STARKDB_VERSION);
        return -1;
    }

    pager->valid_version = version;
    return 0;
}

Pager *pager_open_mode(const char *filename, int readonly) {
    Pager *pager = calloc(1, sizeof(Pager));
    if (!pager) return NULL;

    for (int i = 0; i < TABLE_MAX_PAGES; i++) {
        pager->pages[i] = NULL;
    }

    pager->page_size = PAGE_SIZE;
    pager->valid_version = STARKDB_VERSION;
    pager->readonly = readonly ? 1 : 0;

    pager->file = fopen(filename, readonly ? "rb" : "rb+");
    if (!pager->file && !readonly) {
        pager->file = fopen(filename, "wb+");
        if (!pager->file) {
            free(pager);
            return NULL;
        }
    }
    if (!pager->file) {
        free(pager);
        return NULL;
    }

    fseek(pager->file, 0, SEEK_END);
    long file_size = ftell(pager->file);

    if (file_size == 0) {
        if (readonly) {
            fclose(pager->file);
            free(pager);
            return NULL;
        }
        // Fresh file: write header immediately.
        if (pager_write_header(pager) != 0) {
            fclose(pager->file);
            free(pager);
            return NULL;
        }
        pager->num_pages = 0;
        return pager;
    }

    if (file_size < HEADER_SIZE) {
        fprintf(stderr, "STARKDB: file '%s' is too small to be valid\n", filename);
        fclose(pager->file);
        free(pager);
        return NULL;
    }

    if (pager_read_header(pager, filename) != 0) {
        fclose(pager->file);
        free(pager);
        return NULL;
    }

    long data_bytes = file_size - HEADER_SIZE;
    if (data_bytes % PAGE_SIZE != 0) {
        fprintf(stderr, "STARKDB: file '%s' is corrupt (size not a multiple of page size)\n",
                filename);
        fclose(pager->file);
        free(pager);
        return NULL;
    }

    pager->num_pages = (page_num_t)(data_bytes / PAGE_SIZE);
    return pager;
}

Pager *pager_open(const char *filename) {
    return pager_open_mode(filename, 0);
}

void pager_close(Pager *pager) {
    if (!pager) return;

    for (int i = 0; i < TABLE_MAX_PAGES; i++) {
        if (pager->pages[i]) {
            pager_flush_page(pager, i);
            free(pager->pages[i]);
            pager->pages[i] = NULL;
        }
    }

    fclose(pager->file);
    free(pager);
}

void *pager_get_page(Pager *pager, page_num_t page_num) {
    if (page_num >= TABLE_MAX_PAGES) return NULL;

    if (!pager->pages[page_num]) {
        void *page = calloc(1, pager->page_size);
        if (!page) return NULL;

        if (fseek(pager->file, (long)page_file_offset(page_num), SEEK_SET) != 0) {
            free(page);
            return NULL;
        }

        size_t bytes_read = fread(page, pager->page_size, 1, pager->file);
        if (bytes_read < 1 && !feof(pager->file)) {
            free(page);
            return NULL;
        }

        pager->pages[page_num] = page;

        if (page_num >= pager->num_pages) {
            pager->num_pages = page_num + 1;
        }
    }

    return pager->pages[page_num];
}

DB_Result pager_flush_page(Pager *pager, page_num_t page_num) {
    if (!pager->pages[page_num]) return DB_SUCCESS;
    if (pager->readonly) return DB_SUCCESS;
    if (pager->flush_suppressed) return DB_SUCCESS;

    if (fseek(pager->file, (long)page_file_offset(page_num), SEEK_SET) != 0) {
        return DB_IO_ERROR;
    }

    size_t bytes_written = fwrite(pager->pages[page_num],
                                  pager->page_size, 1, pager->file);
    if (bytes_written < 1) {
        return DB_IO_ERROR;
    }

    return DB_SUCCESS;
}

DB_Result pager_flush_all(Pager *pager) {
    if (pager->readonly) return DB_SUCCESS;
    if (pager->flush_suppressed) return DB_SUCCESS;

    for (int i = 0; i < TABLE_MAX_PAGES; i++) {
        if (pager->pages[i]) {
            pager_flush_page(pager, i);
        }
    }

    if (fflush(pager->file) != 0) {
        return DB_IO_ERROR;
    }

    return DB_SUCCESS;
}

page_num_t pager_allocate_page(Pager *pager) {
    // New pages always get the next page number (never reuse a cache slot that
    // merely happens to be uncached, which could alias a live on-disk page).
    if (pager->num_pages >= TABLE_MAX_PAGES) return INVALID_PAGE;

    page_num_t page_num = pager->num_pages;
    if (!pager->pages[page_num]) {
        pager->pages[page_num] = calloc(1, pager->page_size);
        if (!pager->pages[page_num]) return INVALID_PAGE;
    }
    pager->num_pages = page_num + 1;
    return page_num;
}

uint64_t pager_file_size(Pager *pager) {
    if (!pager || !pager->file) return 0;
    long cur = ftell(pager->file);
    if (fseek(pager->file, 0, SEEK_END) != 0) return 0;
    long size = ftell(pager->file);
    if (cur >= 0) fseek(pager->file, cur, SEEK_SET);
    return size > 0 ? (uint64_t)size : 0;
}

DB_Result pager_truncate(Pager *pager, uint64_t size) {
    if (!pager || !pager->file) return DB_IO_ERROR;
    if (pager->readonly) return DB_IO_ERROR;
    if (fflush(pager->file) != 0) return DB_IO_ERROR;
    if (ftruncate(fileno(pager->file), (off_t)size) != 0) return DB_IO_ERROR;
    fseek(pager->file, 0, SEEK_END);
    return DB_SUCCESS;
}

DB_Result pager_fsync(Pager *pager) {
    if (!pager || !pager->file) return DB_IO_ERROR;
    if (pager->readonly) return DB_SUCCESS;
    if (fflush(pager->file) != 0) return DB_IO_ERROR;
    if (fsync(fileno(pager->file)) != 0) return DB_IO_ERROR;
    return DB_SUCCESS;
}

void pager_invalidate_all(Pager *pager) {
    if (!pager) return;
    for (int i = 0; i < TABLE_MAX_PAGES; i++) {
        if (pager->pages[i]) {
            free(pager->pages[i]);
            pager->pages[i] = NULL;
        }
    }
    // Recompute the page count from the current on-disk size.
    uint64_t size = pager_file_size(pager);
    pager->num_pages = (size >= HEADER_SIZE)
        ? (page_num_t)((size - HEADER_SIZE) / PAGE_SIZE)
        : 0;
}

void pager_set_flush_suppressed(Pager *pager, int suppressed) {
    if (pager) pager->flush_suppressed = suppressed;
}

int pager_acquire_lock(const char *filename, int create, int shared, int *out_fd) {
    if (out_fd) *out_fd = -1;
#ifdef _WIN32
    // TODO: implement LockFileEx/UnlockFileEx here.  On this platform the
    // database currently has no cross-process exclusion.
#if defined(_MSC_VER)
#pragma message("STARKDB: file locking not implemented on this platform")
#else
#warning "file locking not implemented on this platform"
#endif
    (void)filename;
    (void)create;
    (void)shared;
    return 0;
#else
    if (!filename) {
        errno = EINVAL;
        return -1;
    }
    int flags = shared ? O_RDONLY : O_RDWR;
    if (create) flags |= O_CREAT;
    int fd = open(filename, flags, 0644);
    if (fd < 0) return -1;

    int op = (shared ? LOCK_SH : LOCK_EX) | LOCK_NB;
    if (flock(fd, op) != 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }

    if (out_fd) *out_fd = fd;
    return 0;
#endif
}

void pager_release_lock(int fd) {
    if (fd < 0) return;
#ifndef _WIN32
    close(fd);
#else
    (void)fd;
#endif
}
