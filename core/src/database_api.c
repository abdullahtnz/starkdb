#include "stark.h"
#include "database.h"
#include "btree.h"
#include "storage.h"
#include "type.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>

struct stark_db {
    Database* internal_db;
    char last_error[256];
    char* path;
    int in_transaction;
    uint64_t txn_idx_size;
    uint64_t txn_dat_size;
    uint32_t mod_count;        // incremented by every mutator
    stark_cursor_t* cursors;   // live cursors, so close() can invalidate them
    int lock_fd;               // fd of .idx for flock, or -1
    int is_readonly;           // 1 if opened STARK_OPEN_READONLY
};

// Most recent failed stark_open_ex()/stark_open().  Single static buffer:
// stark_last_open_error() is explicitly documented as not thread-safe.
static char g_last_open_error[512];
static int g_last_open_error_set = 0;

static void set_open_error(stark_open_error_t code, const char* fmt, ...) {
    (void)code;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_last_open_error, sizeof(g_last_open_error), fmt, ap);
    va_end(ap);
    g_last_open_error_set = 1;
}

static void clear_open_error(void) {
    g_last_open_error[0] = '\0';
    g_last_open_error_set = 0;
}

// Mark every live cursor as closed (its page cache is about to disappear).
// Defined after struct stark_cursor.
static void db_invalidate_cursors(stark_db_t* db);

// Bump the modification counter (invalidates all existing cursors).
static void db_touch(stark_db_t* db) {
    if (db) db->mod_count++;
}

static int reject_readonly(stark_db_t* db) {
    if (db && db->is_readonly) {
        snprintf(db->last_error, sizeof(db->last_error), "database opened read-only");
        return 1;
    }
    return 0;
}

static int path_join(char* out, size_t out_size, const char* base,
                     const char* suffix) {
    int n = snprintf(out, out_size, "%s%s", base, suffix);
    return (n > 0 && (size_t)n < out_size) ? 0 : -1;
}

// ==================== FILE HEADER HELPERS ====================

// Inspect and validate a component file's header.
// Returns STARK_OPEN_OK, STARK_OPEN_ERR_CORRUPT, STARK_OPEN_ERR_VERSION,
// STARK_OPEN_ERR_READONLY or STARK_OPEN_ERR_IO.  A zero-length file is valid
// for a writer (the pager writes the header) but invalid for a read-only open.
static stark_open_error_t classify_file(const char* filename, int readonly,
                                        char* msg, size_t msg_size) {
    FILE* f = fopen(filename, "rb");
    if (!f) {
        int e = errno;
        if (e == EACCES || e == EROFS) {
            snprintf(msg, msg_size, "cannot open %s: %s", filename, strerror(e));
            return STARK_OPEN_ERR_READONLY;
        }
        snprintf(msg, msg_size, "cannot open %s: %s", filename, strerror(e));
        return STARK_OPEN_ERR_IO;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        snprintf(msg, msg_size, "cannot seek %s", filename);
        return STARK_OPEN_ERR_IO;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        snprintf(msg, msg_size, "cannot size %s", filename);
        return STARK_OPEN_ERR_IO;
    }

    if (size == 0) {
        fclose(f);
        if (readonly) {
            snprintf(msg, msg_size, "%s is empty", filename);
            return STARK_OPEN_ERR_CORRUPT;
        }
        return STARK_OPEN_OK;
    }

    if (size < HEADER_SIZE) {
        fclose(f);
        snprintf(msg, msg_size, "not a STARKDB file (missing magic)");
        return STARK_OPEN_ERR_CORRUPT;
    }

    uint8_t header[HEADER_SIZE];
    if (fseek(f, 0, SEEK_SET) != 0 ||
        fread(header, 1, HEADER_SIZE, f) != HEADER_SIZE) {
        fclose(f);
        snprintf(msg, msg_size, "cannot read %s", filename);
        return STARK_OPEN_ERR_IO;
    }
    fclose(f);

    if (memcmp(header, STARKDB_MAGIC, STARKDB_MAGIC_LEN) != 0) {
        snprintf(msg, msg_size, "not a STARKDB file (missing magic)");
        return STARK_OPEN_ERR_CORRUPT;
    }

    uint32_t version;
    memcpy(&version, header + 8, sizeof(uint32_t));
    if (version > STARKDB_VERSION) {
        snprintf(msg, msg_size,
                 "file version %u is newer than supported version %u",
                 version, STARKDB_VERSION);
        return STARK_OPEN_ERR_VERSION;
    }

    if ((size - HEADER_SIZE) % PAGE_SIZE != 0) {
        snprintf(msg, msg_size, "file size is not a multiple of the page size");
        return STARK_OPEN_ERR_CORRUPT;
    }

    return STARK_OPEN_OK;
}

static int file_exists(const char* filename) {
    struct stat st;
    return (stat(filename, &st) == 0 && S_ISREG(st.st_mode));
}

static int path_is_directory(const char* path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

static int same_file(const char* a, const char* b) {
    struct stat sa, sb;
    if (stat(a, &sa) != 0 || stat(b, &sb) != 0) return 0;
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

// True when the fd still refers to the file currently at path.
static int lock_matches_path(int fd, const char* path) {
    if (fd < 0) return 1;
    struct stat st_fd, st_path;
    if (fstat(fd, &st_fd) != 0) return 0;
    if (stat(path, &st_path) != 0) return 0;
    return st_fd.st_dev == st_path.st_dev && st_fd.st_ino == st_path.st_ino;
}

static void fsync_parent_dir(const char* path) {
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", path);
    dir[sizeof(dir) - 1] = '\0';
    char* slash = strrchr(dir, '/');
    if (slash) *slash = '\0';
    else snprintf(dir, sizeof(dir), ".");
    int fd = open(dir, O_RDONLY);
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }
}

// Upgrade a single legacy file (no header) in place via temp-file + rename.
static stark_result_t upgrade_one_file(const char* filename) {
    FILE* in = fopen(filename, "rb");
    if (!in) return STARK_OK;  // nothing to upgrade

    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    fseek(in, 0, SEEK_SET);

    if (size >= HEADER_SIZE) {
        uint8_t magic[STARKDB_MAGIC_LEN];
        if (fread(magic, 1, STARKDB_MAGIC_LEN, in) == STARKDB_MAGIC_LEN &&
            memcmp(magic, STARKDB_MAGIC, STARKDB_MAGIC_LEN) == 0) {
            fclose(in);
            return STARK_OK;  // already has header
        }
        fseek(in, 0, SEEK_SET);
    }

    if (size == 0) {
        // Empty file: just drop a header in.
        fclose(in);
        in = NULL;
    } else if (size % PAGE_SIZE != 0) {
        fclose(in);
        fprintf(stderr, "STARKDB: cannot upgrade '%s' (size not a multiple of page size)\n",
                filename);
        return STARK_ERROR;
    }

    char tmp_name[1024];
    snprintf(tmp_name, sizeof(tmp_name), "%s.tmp", filename);
    FILE* out = fopen(tmp_name, "wb+");
    if (!out) {
        if (in) fclose(in);
        return STARK_ERROR;
    }

    uint8_t header[HEADER_SIZE];
    memcpy(header, STARKDB_MAGIC, STARKDB_MAGIC_LEN);
    uint32_t version = STARKDB_VERSION;
    uint32_t flags = 0;
    memcpy(header + 8, &version, sizeof(uint32_t));
    memcpy(header + 12, &flags, sizeof(uint32_t));
    if (fwrite(header, 1, HEADER_SIZE, out) != HEADER_SIZE) goto fail;

    if (in) {
        char buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
            if (fwrite(buf, 1, n, out) != n) goto fail;
        }
        fclose(in);
        in = NULL;
    }

    fflush(out);
    if (fsync(fileno(out)) != 0) goto fail;
    fclose(out);
    out = NULL;

    if (rename(tmp_name, filename) != 0) {
        remove(tmp_name);
        return STARK_ERROR;
    }
    return STARK_OK;

fail:
    if (in) fclose(in);
    if (out) fclose(out);
    remove(tmp_name);
    return STARK_ERROR;
}

STARK_API stark_result_t stark_upgrade_file(const char* path) {
    if (!path) return STARK_INVALID_ARG;

    char idx_name[1024];
    char dat_name[1024];
    snprintf(idx_name, sizeof(idx_name), "%s.idx", path);
    snprintf(dat_name, sizeof(dat_name), "%s.dat", path);

    if (upgrade_one_file(idx_name) != STARK_OK) return STARK_ERROR;
    if (upgrade_one_file(dat_name) != STARK_OK) return STARK_ERROR;
    return STARK_OK;
}

// ==================== TRANSACTION SENTINEL ====================

#define TXN_SENTINEL_MAGIC "STARKTXN"

static int txn_sentinel_path(const char* base, char* out, size_t out_size) {
    int n = snprintf(out, out_size, "%s.txn", base);
    return (n > 0 && (size_t)n < out_size) ? 0 : -1;
}

static int write_txn_sentinel(stark_db_t* db) {
    char name[1024];
    if (txn_sentinel_path(db->path, name, sizeof(name)) != 0) return -1;

    FILE* f = fopen(name, "wb");
    if (!f) return -1;
    fprintf(f, "%llu\n%llu\n%s\n",
            (unsigned long long)db->txn_idx_size,
            (unsigned long long)db->txn_dat_size,
            TXN_SENTINEL_MAGIC);
    fflush(f);
    if (fsync(fileno(f)) != 0) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static void delete_txn_sentinel(const char* base) {
    char name[1024];
    if (txn_sentinel_path(base, name, sizeof(name)) == 0) {
        remove(name);
    }
}

static void truncate_named_file(const char* filename, uint64_t size) {
    FILE* f = fopen(filename, "rb+");
    if (!f) return;  // missing file: nothing to truncate
    if (fflush(f) == 0) {
        (void)ftruncate(fileno(f), (off_t)size);
    }
    fclose(f);
}

// Crash recovery: if a .txn sentinel is present, roll back both component files
// to the captured sizes and delete the sentinel. Returns 0 on success (or no
// sentinel), -1 on a malformed sentinel.
static int recover_txn_sentinel(const char* path) {
    char name[1024];
    if (txn_sentinel_path(path, name, sizeof(name)) != 0) return 0;

    FILE* f = fopen(name, "rb");
    if (!f) return 0;  // no sentinel

    char buf[256];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    unsigned long long idx_size = 0, dat_size = 0;
    int consumed = 0;
    if (sscanf(buf, "%llu %llu %n", &idx_size, &dat_size, &consumed) < 2 ||
        strncmp(buf + consumed, TXN_SENTINEL_MAGIC, strlen(TXN_SENTINEL_MAGIC)) != 0) {
        fprintf(stderr, "STARKDB: invalid transaction sentinel '%s' (bad magic)\n", name);
        return -1;
    }

    char idx_name[1024], dat_name[1024];
    snprintf(idx_name, sizeof(idx_name), "%s.idx", path);
    snprintf(dat_name, sizeof(dat_name), "%s.dat", path);
    truncate_named_file(idx_name, (uint64_t)idx_size);
    truncate_named_file(dat_name, (uint64_t)dat_size);
    remove(name);
    return 0;
}

// ==================== COMPACTION SENTINEL ====================

static int compact_file_exists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

// Crash recovery for an interrupted compaction. Always completes the swap
// forward (towards the freshly built files) or, if no rename had started,
// discards the new files and keeps the intact originals.
static int recover_compact_sentinel(const char* path) {
    char sentinel[1024];
    snprintf(sentinel, sizeof(sentinel), "%s.compact", path);

    FILE* f = fopen(sentinel, "rb");
    if (!f) return 0;  // no sentinel

    char buf[64];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    if (strncmp(buf, COMPACT_SENTINEL_MAGIC, strlen(COMPACT_SENTINEL_MAGIC)) != 0) {
        fprintf(stderr, "STARKDB: invalid compaction sentinel '%s'\n", sentinel);
        return -1;
    }

    char idx_path[1024], dat_path[1024];
    char idx_new[1024], dat_new[1024];
    char idx_old[1024], dat_old[1024];
    snprintf(idx_path, sizeof(idx_path), "%s.idx", path);
    snprintf(dat_path, sizeof(dat_path), "%s.dat", path);
    snprintf(idx_new, sizeof(idx_new), "%s.idx.new", path);
    snprintf(dat_new, sizeof(dat_new), "%s.dat.new", path);
    snprintf(idx_old, sizeof(idx_old), "%s.idx.old", path);
    snprintf(dat_old, sizeof(dat_old), "%s.dat.old", path);

    int have_old = compact_file_exists(idx_old) || compact_file_exists(dat_old);

    if (!have_old) {
        // No rename has happened yet: originals are intact. Discard temp files.
        remove(idx_new);
        remove(dat_new);
        remove(sentinel);
        if (!compact_file_exists(idx_path) || !compact_file_exists(dat_path)) {
            fprintf(stderr, "STARKDB: compaction interrupted in unrecoverable state\n");
            return -1;
        }
        return 0;
    }

    // Renames have started: finish moving the new files into place.
    if (compact_file_exists(idx_new)) {
        if (compact_file_exists(idx_path) && !compact_file_exists(idx_old)) {
            rename(idx_path, idx_old);
        }
        if (compact_file_exists(idx_path)) remove(idx_path);
        if (rename(idx_new, idx_path) != 0) { /* fall through to validation */ }
    }
    if (compact_file_exists(dat_new)) {
        if (compact_file_exists(dat_path) && !compact_file_exists(dat_old)) {
            rename(dat_path, dat_old);
        }
        if (compact_file_exists(dat_path)) remove(dat_path);
        if (rename(dat_new, dat_path) != 0) { /* fall through to validation */ }
    }

    if (!compact_file_exists(idx_path) || !compact_file_exists(dat_path)) {
        fprintf(stderr, "STARKDB: compaction interrupted in unrecoverable state\n");
        return -1;
    }

    remove(idx_old);
    remove(dat_old);
    remove(sentinel);
    return 0;
}

// ==================== LIFECYCLE ====================

static stark_result_t open_fail(stark_open_error_t* out_err,
                                stark_open_error_t code, const char* msg) {
    if (out_err) *out_err = code;
    set_open_error(code, "%s", msg);
    return STARK_ERROR;
}

STARK_API stark_result_t stark_open_ex(stark_db_t** out_db, const char* path,
                                       uint32_t flags, stark_open_error_t* out_err) {
    if (!out_db || !out_err) {
        if (out_db) *out_db = NULL;
        if (out_err) *out_err = STARK_OPEN_ERR_INVALID_ARG;
        set_open_error(STARK_OPEN_ERR_INVALID_ARG, "invalid argument");
        return STARK_INVALID_ARG;
    }
    *out_db = NULL;
    if (!path || path[0] == '\0') {
        *out_err = STARK_OPEN_ERR_INVALID_ARG;
        set_open_error(STARK_OPEN_ERR_INVALID_ARG, "invalid path");
        return STARK_INVALID_ARG;
    }
    *out_err = STARK_OPEN_ERR_INVALID_ARG;

    int excl = (flags & STARK_OPEN_EXCL) != 0;
    int readonly = (flags & STARK_OPEN_READONLY) != 0;

    char idx_name[1024], dat_name[1024], compact_name[1024], txn_name[1024];
    if (path_join(idx_name, sizeof(idx_name), path, ".idx") != 0 ||
        path_join(dat_name, sizeof(dat_name), path, ".dat") != 0 ||
        path_join(compact_name, sizeof(compact_name), path, ".compact") != 0 ||
        path_join(txn_name, sizeof(txn_name), path, ".txn") != 0) {
        return open_fail(out_err, STARK_OPEN_ERR_INVALID_ARG, "path too long");
    }

    int idx_exists = file_exists(idx_name);
    int dat_exists = file_exists(dat_name);
    int sentinel_exists = file_exists(compact_name) || file_exists(txn_name);
    int fresh = (!idx_exists && !dat_exists && !sentinel_exists);
    int lock_fd = -1;

    if (idx_exists) {
        if (pager_acquire_lock(idx_name, 0, readonly, &lock_fd) != 0) {
            int e = errno;
            if (e == EWOULDBLOCK || e == EAGAIN) {
                return open_fail(out_err, STARK_OPEN_ERR_LOCKED,
                                 "database is locked by another process");
            }
            char msg[1200];
            snprintf(msg, sizeof(msg), "cannot open %s: %s", idx_name, strerror(e));
            return open_fail(out_err,
                             (e == EACCES || e == EROFS) ? STARK_OPEN_ERR_READONLY
                                                         : STARK_OPEN_ERR_IO,
                             msg);
        }
    } else if (sentinel_exists) {
        // Crash recovery removed/renamed the .idx path.  The crashed process
        // has released its lock, so recover first, then lock the installed
        // index.  (A read-only handle cannot safely recover.)
        if (readonly) {
            return open_fail(out_err, STARK_OPEN_ERR_CORRUPT,
                             "database requires recovery; open it read-write first");
        }
        if (recover_compact_sentinel(path) != 0 || recover_txn_sentinel(path) != 0) {
            return open_fail(out_err, STARK_OPEN_ERR_CORRUPT,
                             "database recovery failed");
        }
        idx_exists = file_exists(idx_name);
        dat_exists = file_exists(dat_name);
        if (!idx_exists) {
            return open_fail(out_err, STARK_OPEN_ERR_CORRUPT,
                             "database files are missing");
        }
        if (pager_acquire_lock(idx_name, 0, 0, &lock_fd) != 0) {
            int e = errno;
            if (e == EWOULDBLOCK || e == EAGAIN) {
                return open_fail(out_err, STARK_OPEN_ERR_LOCKED,
                                 "database is locked by another process");
            }
            char msg[1200];
            snprintf(msg, sizeof(msg), "cannot open %s: %s", idx_name, strerror(e));
            return open_fail(out_err,
                             (e == EACCES || e == EROFS) ? STARK_OPEN_ERR_READONLY
                                                         : STARK_OPEN_ERR_IO,
                             msg);
        }
    } else if (dat_exists) {
        return open_fail(out_err, STARK_OPEN_ERR_CORRUPT, "idx file missing");
    } else {
        // Neither component exists.
        if (excl) {
            return open_fail(out_err, STARK_OPEN_ERR_NOT_FOUND,
                             "database does not exist (STARK_OPEN_EXCL)");
        }
        if (readonly) {
            return open_fail(out_err, STARK_OPEN_ERR_NOT_FOUND,
                             "database does not exist");
        }
        if (pager_acquire_lock(idx_name, 1, 0, &lock_fd) != 0) {
            int e = errno;
            char msg[1200];
            snprintf(msg, sizeof(msg), "cannot create %s: %s", idx_name, strerror(e));
            return open_fail(out_err,
                             (e == EACCES || e == EROFS) ? STARK_OPEN_ERR_READONLY
                                                         : STARK_OPEN_ERR_IO,
                             msg);
        }
    }

    // Recover while holding the lock.  When the sentinel branch above already
    // recovered, there is no sentinel left, so these calls are cheap no-ops.
    if (!readonly) {
        if (recover_compact_sentinel(path) != 0) {
            pager_release_lock(lock_fd);
            return open_fail(out_err, STARK_OPEN_ERR_CORRUPT,
                             "database recovery failed (compaction)");
        }
        if (recover_txn_sentinel(path) != 0) {
            pager_release_lock(lock_fd);
            return open_fail(out_err, STARK_OPEN_ERR_CORRUPT,
                             "database recovery failed (transaction)");
        }
    } else if (sentinel_exists) {
        pager_release_lock(lock_fd);
        return open_fail(out_err, STARK_OPEN_ERR_CORRUPT,
                         "database requires recovery; open it read-write first");
    }

    // Recovery may have renamed a freshly built .idx over the original one.
    // The flock follows the inode, so re-acquire the lock on the installed file.
    if (!lock_matches_path(lock_fd, idx_name)) {
        pager_release_lock(lock_fd);
        lock_fd = -1;
        if (pager_acquire_lock(idx_name, 0, readonly, &lock_fd) != 0) {
            int e = errno;
            if (e == EWOULDBLOCK || e == EAGAIN) {
                return open_fail(out_err, STARK_OPEN_ERR_LOCKED,
                                 "database is locked by another process");
            }
            char msg[1200];
            snprintf(msg, sizeof(msg), "cannot reopen %s: %s", idx_name, strerror(e));
            return open_fail(out_err,
                             (e == EACCES || e == EROFS) ? STARK_OPEN_ERR_READONLY
                                                         : STARK_OPEN_ERR_IO,
                             msg);
        }
    }

    idx_exists = file_exists(idx_name);
    dat_exists = file_exists(dat_name);
    if (!fresh) {
        if (idx_exists && !dat_exists) {
            pager_release_lock(lock_fd);
            return open_fail(out_err, STARK_OPEN_ERR_CORRUPT, "dat file missing");
        }
        if (!idx_exists && dat_exists) {
            pager_release_lock(lock_fd);
            return open_fail(out_err, STARK_OPEN_ERR_CORRUPT, "idx file missing");
        }
        if (!idx_exists && !dat_exists) {
            pager_release_lock(lock_fd);
            return open_fail(out_err, STARK_OPEN_ERR_CORRUPT,
                             "database files are missing");
        }
    }

    char msg[1200];
    if (idx_exists) {
        stark_open_error_t ce = classify_file(idx_name, readonly, msg, sizeof(msg));
        if (ce != STARK_OPEN_OK) {
            pager_release_lock(lock_fd);
            return open_fail(out_err, ce, msg);
        }
    }
    if (dat_exists) {
        stark_open_error_t ce = classify_file(dat_name, readonly, msg, sizeof(msg));
        if (ce != STARK_OPEN_OK) {
            pager_release_lock(lock_fd);
            return open_fail(out_err, ce, msg);
        }
    }

    stark_db_t* db = (stark_db_t*)calloc(1, sizeof(stark_db_t));
    if (!db) {
        pager_release_lock(lock_fd);
        return open_fail(out_err, STARK_OPEN_ERR_IO, "out of memory");
    }
    db->path = strdup(path);
    db->lock_fd = lock_fd;
    db->is_readonly = readonly;
    if (!db->path) {
        pager_release_lock(lock_fd);
        free(db);
        return open_fail(out_err, STARK_OPEN_ERR_IO, "out of memory");
    }

    db->internal_db = db_open_mode(path, readonly);
    if (!db->internal_db) {
        snprintf(db->last_error, sizeof(db->last_error),
                 "Failed to open database: %s", path);
        pager_release_lock(lock_fd);
        free(db->path);
        free(db);
        return open_fail(out_err, STARK_OPEN_ERR_IO, "failed to open database");
    }

    *out_db = db;
    *out_err = STARK_OPEN_OK;
    clear_open_error();
    return STARK_OK;
}

STARK_API stark_db_t* stark_open(const char* path, unsigned flags) {
    stark_db_t* db = NULL;
    stark_open_error_t err = STARK_OPEN_ERR_INVALID_ARG;
    if (stark_open_ex(&db, path, (uint32_t)flags, &err) != STARK_OK) {
        return NULL;
    }
    return db;
}

STARK_API const char* stark_last_open_error(void) {
    return g_last_open_error_set ? g_last_open_error : NULL;
}

STARK_API void stark_close(stark_db_t* db) {
    if (!db) return;
    if (db->internal_db) {
        if (db->in_transaction) {
            // An uncommitted transaction is discarded on close.
            stark_rollback(db);
        }
        stark_sync(db);
        db_invalidate_cursors(db);
        db_close(db->internal_db);
    }
    pager_release_lock(db->lock_fd);
    free(db->path);
    free(db);
}

// ==================== BACKUP / RESTORE ====================

static stark_result_t copy_file(const char* src, const char* dst) {
    FILE* in = fopen(src, "rb");
    if (!in) return STARK_IO_ERROR;
    FILE* out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return STARK_IO_ERROR;
    }

    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) goto fail;
    }
    if (ferror(in)) goto fail;
    if (fflush(out) != 0) goto fail;
    if (fsync(fileno(out)) != 0) goto fail;

    fclose(in);
    fclose(out);
    return STARK_OK;

fail:
    fclose(in);
    fclose(out);
    remove(dst);
    return STARK_IO_ERROR;
}

static stark_result_t install_temp(const char* tmp, const char* dst) {
    if (rename(tmp, dst) == 0) return STARK_OK;
    if (errno != EXDEV) return STARK_IO_ERROR;
    // Different filesystems: rename cannot be atomic.  Fall back to a copy.
    stark_result_t r = copy_file(tmp, dst);
    if (r == STARK_OK) remove(tmp);
    return r;
}

static long component_size(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long)st.st_size;
}

STARK_API stark_result_t stark_backup(stark_db_t* db, const char* dest_prefix) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (!dest_prefix || dest_prefix[0] == '\0') {
        snprintf(db->last_error, sizeof(db->last_error), "invalid destination path");
        return STARK_INVALID_ARG;
    }
    if (db->in_transaction) {
        snprintf(db->last_error, sizeof(db->last_error),
                 "cannot backup during transaction");
        return STARK_ERROR;
    }
    if (path_is_directory(dest_prefix)) {
        snprintf(db->last_error, sizeof(db->last_error),
                 "destination path is a directory");
        return STARK_IO_ERROR;
    }
    if (stark_sync(db) != STARK_OK) {
        snprintf(db->last_error, sizeof(db->last_error), "failed to sync before backup");
        return STARK_ERROR;
    }

    char src_idx[1024], src_dat[1024];
    char dst_idx[1024], dst_dat[1024];
    char dst_idx_tmp[1024], dst_dat_tmp[1024];
    if (path_join(src_idx, sizeof(src_idx), db->path, ".idx") != 0 ||
        path_join(src_dat, sizeof(src_dat), db->path, ".dat") != 0 ||
        path_join(dst_idx, sizeof(dst_idx), dest_prefix, ".idx") != 0 ||
        path_join(dst_dat, sizeof(dst_dat), dest_prefix, ".dat") != 0 ||
        path_join(dst_idx_tmp, sizeof(dst_idx_tmp), dest_prefix, ".idx.tmp") != 0 ||
        path_join(dst_dat_tmp, sizeof(dst_dat_tmp), dest_prefix, ".dat.tmp") != 0) {
        snprintf(db->last_error, sizeof(db->last_error), "path too long");
        return STARK_INVALID_ARG;
    }

    if (same_file(src_idx, dst_idx)) {
        snprintf(db->last_error, sizeof(db->last_error),
                 "source and destination are the same database");
        return STARK_INVALID_ARG;
    }

    int dest_lock = -1;
    if (file_exists(dst_idx)) {
        if (pager_acquire_lock(dst_idx, 0, 0, &dest_lock) != 0) {
            int e = errno;
            snprintf(db->last_error, sizeof(db->last_error),
                     (e == EWOULDBLOCK || e == EAGAIN)
                         ? "destination is in use"
                         : "cannot lock destination");
            return (e == EWOULDBLOCK || e == EAGAIN) ? STARK_ERROR : STARK_IO_ERROR;
        }
    }

    remove(dst_idx_tmp);
    remove(dst_dat_tmp);

    if (copy_file(src_idx, dst_idx_tmp) != STARK_OK ||
        copy_file(src_dat, dst_dat_tmp) != STARK_OK) {
        remove(dst_idx_tmp);
        remove(dst_dat_tmp);
        pager_release_lock(dest_lock);
        snprintf(db->last_error, sizeof(db->last_error), "backup copy failed");
        return STARK_IO_ERROR;
    }

    if (install_temp(dst_idx_tmp, dst_idx) != STARK_OK ||
        install_temp(dst_dat_tmp, dst_dat) != STARK_OK) {
        remove(dst_idx_tmp);
        remove(dst_dat_tmp);
        pager_release_lock(dest_lock);
        snprintf(db->last_error, sizeof(db->last_error), "backup install failed");
        return STARK_IO_ERROR;
    }

    fsync_parent_dir(dst_idx);
    pager_release_lock(dest_lock);
    return STARK_OK;
}

STARK_API stark_result_t stark_restore(const char* src_prefix, const char* dest_prefix) {
    if (!src_prefix || !dest_prefix || src_prefix[0] == '\0' || dest_prefix[0] == '\0')
        return STARK_INVALID_ARG;
    if (path_is_directory(dest_prefix)) return STARK_IO_ERROR;

    char src_idx[1024], src_dat[1024];
    char dst_idx[1024], dst_dat[1024];
    char dst_idx_tmp[1024], dst_dat_tmp[1024];
    if (path_join(src_idx, sizeof(src_idx), src_prefix, ".idx") != 0 ||
        path_join(src_dat, sizeof(src_dat), src_prefix, ".dat") != 0 ||
        path_join(dst_idx, sizeof(dst_idx), dest_prefix, ".idx") != 0 ||
        path_join(dst_dat, sizeof(dst_dat), dest_prefix, ".dat") != 0 ||
        path_join(dst_idx_tmp, sizeof(dst_idx_tmp), dest_prefix, ".idx.tmp") != 0 ||
        path_join(dst_dat_tmp, sizeof(dst_dat_tmp), dest_prefix, ".dat.tmp") != 0) {
        return STARK_INVALID_ARG;
    }

    if (same_file(src_idx, dst_idx)) return STARK_INVALID_ARG;

    if (!file_exists(src_idx) && !file_exists(src_dat))
        return STARK_ERROR;
    if (!file_exists(src_idx) || !file_exists(src_dat))
        return STARK_ERROR;

    char msg[1200];
    if (classify_file(src_idx, 1, msg, sizeof(msg)) != STARK_OPEN_OK ||
        classify_file(src_dat, 1, msg, sizeof(msg)) != STARK_OPEN_OK) {
        return STARK_ERROR;
    }

    int dest_lock = -1;
    if (file_exists(dst_idx)) {
        if (!file_exists(dst_dat)) return STARK_ERROR;
        long isz = component_size(dst_idx);
        long dsz = component_size(dst_dat);
        if (!(isz == 0 && dsz == 0)) {
            if (classify_file(dst_idx, 1, msg, sizeof(msg)) != STARK_OPEN_OK ||
                classify_file(dst_dat, 1, msg, sizeof(msg)) != STARK_OPEN_OK) {
                return STARK_ERROR;
            }
        }
        if (pager_acquire_lock(dst_idx, 0, 0, &dest_lock) != 0) {
            int e = errno;
            return (e == EWOULDBLOCK || e == EAGAIN) ? STARK_ERROR : STARK_IO_ERROR;
        }
    } else if (file_exists(dst_dat)) {
        return STARK_ERROR;
    }

    remove(dst_idx_tmp);
    remove(dst_dat_tmp);

    if (copy_file(src_idx, dst_idx_tmp) != STARK_OK ||
        copy_file(src_dat, dst_dat_tmp) != STARK_OK) {
        remove(dst_idx_tmp);
        remove(dst_dat_tmp);
        pager_release_lock(dest_lock);
        return STARK_IO_ERROR;
    }

    if (install_temp(dst_idx_tmp, dst_idx) != STARK_OK ||
        install_temp(dst_dat_tmp, dst_dat) != STARK_OK) {
        remove(dst_idx_tmp);
        remove(dst_dat_tmp);
        pager_release_lock(dest_lock);
        return STARK_IO_ERROR;
    }

    fsync_parent_dir(dst_idx);
    pager_release_lock(dest_lock);
    return STARK_OK;
}

// ==================== CRUD ====================

STARK_API stark_result_t stark_put(stark_db_t* db, uint32_t key,
                                    const void* value, size_t value_size) {
    return stark_add(db, key, value, value_size);
}

STARK_API stark_result_t stark_add(stark_db_t* db, uint32_t key,
                                    const void* value, size_t value_size) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (reject_readonly(db)) return STARK_ERROR;
    if (!value || value_size == 0) return STARK_INVALID_ARG;
    db_touch(db);

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
    if (!buffer_size) return STARK_INVALID_ARG;

    DB_Result result = db_find(db->internal_db, key, buffer, buffer_size);

    switch (result) {
        case DB_SUCCESS: return STARK_OK;
        case DB_NOT_FOUND: return STARK_NOT_FOUND;
        case DB_BUFFER_TOO_SMALL:
            snprintf(db->last_error, sizeof(db->last_error),
                     "buffer too small: required %zu bytes", *buffer_size);
            return STARK_BUFFER_TOO_SMALL;
        default: return STARK_ERROR;
    }
}

STARK_API stark_result_t stark_delete(stark_db_t* db, uint32_t key) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (reject_readonly(db)) return STARK_ERROR;
    db_touch(db);
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
    return (result == DB_SUCCESS) || (result == DB_BUFFER_TOO_SMALL);
}

// ==================== CURSOR ====================

struct stark_cursor {
    stark_db_t* db;
    BTreeCursor* btc;
    int valid;
    uint32_t creation_mod_count;
    struct stark_cursor* next;
};

static void db_invalidate_cursors(stark_db_t* db) {
    if (!db) return;
    stark_cursor_t* c = db->cursors;
    while (c) {
        stark_cursor_t* next = c->next;
        if (c->btc) {
            btree_cursor_destroy(c->btc);
            c->btc = NULL;
        }
        c->db = NULL;
        c = next;
    }
    db->cursors = NULL;
}

// Validate a cursor before an operation. Closed takes precedence over stale.
static stark_result_t cursor_precheck(stark_cursor_t* cursor) {
    if (!cursor) return STARK_INVALID_ARG;
    if (!cursor->db) return STARK_CLOSED;
    if (!cursor->btc) return STARK_INVALID_ARG;
    if (cursor->creation_mod_count != cursor->db->mod_count) {
        snprintf(cursor->db->last_error, sizeof(cursor->db->last_error),
                 "cursor invalidated by intervening modification");
        return STARK_STALE_CURSOR;
    }
    return STARK_OK;
}

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
    cursor->creation_mod_count = db->mod_count;
    cursor->next = db->cursors;
    db->cursors = cursor;
    return cursor;
}

STARK_API stark_result_t stark_cursor_first(stark_cursor_t* cursor) {
    stark_result_t pre = cursor_precheck(cursor);
    if (pre != STARK_OK) return pre;
    DB_Result r = btree_cursor_first(cursor->btc);
    cursor->valid = (r == DB_SUCCESS);
    return (r == DB_SUCCESS) ? STARK_OK : STARK_NOT_FOUND;
}

STARK_API stark_result_t stark_cursor_last(stark_cursor_t* cursor) {
    stark_result_t pre = cursor_precheck(cursor);
    if (pre != STARK_OK) return pre;
    DB_Result r = btree_cursor_last(cursor->btc);
    cursor->valid = (r == DB_SUCCESS);
    return (r == DB_SUCCESS) ? STARK_OK : STARK_NOT_FOUND;
}

STARK_API stark_result_t stark_cursor_next(stark_cursor_t* cursor) {
    stark_result_t pre = cursor_precheck(cursor);
    if (pre != STARK_OK) return pre;
    DB_Result r = btree_cursor_next(cursor->btc);
    cursor->valid = (r == DB_SUCCESS);
    return (r == DB_SUCCESS) ? STARK_OK : STARK_NOT_FOUND;
}

STARK_API stark_result_t stark_cursor_prev(stark_cursor_t* cursor) {
    stark_result_t pre = cursor_precheck(cursor);
    if (pre != STARK_OK) return pre;
    DB_Result r = btree_cursor_prev(cursor->btc);
    cursor->valid = (r == DB_SUCCESS);
    return (r == DB_SUCCESS) ? STARK_OK : STARK_NOT_FOUND;
}

STARK_API stark_result_t stark_cursor_get(stark_cursor_t* cursor,
                                         uint32_t* key,
                                         void* buffer, size_t* buffer_size) {
    stark_result_t pre = cursor_precheck(cursor);
    if (pre != STARK_OK) return pre;
    if (!cursor->valid) return STARK_NOT_FOUND;
    if (!key || !buffer_size) return STARK_INVALID_ARG;

    uint64_t value;
    DB_Result r = btree_cursor_get(cursor->btc, key, &value);
    if (r != DB_SUCCESS) return STARK_NOT_FOUND;

    DB_Result sr = storage_read(cursor->db->internal_db->storage, value,
                                buffer, buffer_size);
    if (sr == DB_BUFFER_TOO_SMALL) {
        snprintf(cursor->db->last_error, sizeof(cursor->db->last_error),
                 "buffer too small: required %zu bytes", *buffer_size);
        return STARK_BUFFER_TOO_SMALL;
    }
    return (sr == DB_SUCCESS) ? STARK_OK : STARK_ERROR;
}

STARK_API void stark_cursor_destroy(stark_cursor_t* cursor) {
    if (!cursor) return;
    if (cursor->db) {
        // Unlink from the owning database's cursor list.
        stark_cursor_t** pp = &cursor->db->cursors;
        while (*pp) {
            if (*pp == cursor) {
                *pp = cursor->next;
                break;
            }
            pp = &(*pp)->next;
        }
    }
    if (cursor->btc) btree_cursor_destroy(cursor->btc);
    free(cursor);
}

STARK_API stark_result_t stark_foreach_range(stark_db_t* db,
                                             uint32_t key_min, uint32_t key_max,
                                             stark_foreach_cb cb, void* user) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (!cb) return STARK_INVALID_ARG;
    if (key_min >= key_max) return STARK_OK;

    stark_cursor_t* cursor = stark_cursor_create(db);
    if (!cursor) {
        snprintf(db->last_error, sizeof(db->last_error),
                 "foreach: failed to create cursor");
        return STARK_MEMORY_ERROR;
    }

    stark_result_t result = STARK_OK;
    DB_Result seek = btree_cursor_seek(cursor->btc, key_min);
    if (seek == DB_NOT_FOUND) {
        stark_cursor_destroy(cursor);
        return STARK_OK;
    }
    if (seek != DB_SUCCESS) {
        snprintf(db->last_error, sizeof(db->last_error), "foreach: seek failed");
        stark_cursor_destroy(cursor);
        return STARK_ERROR;
    }

    for (;;) {
        uint32_t key = 0;
        uint64_t addr = 0;
        if (btree_cursor_get(cursor->btc, &key, &addr) != DB_SUCCESS) {
            snprintf(db->last_error, sizeof(db->last_error), "foreach: cursor read failed");
            result = STARK_ERROR;
            break;
        }
        if (key >= key_max) break;

        if (key >= key_min) {
            size_t value_size = 0;
            void* value = storage_get_ptr(db->internal_db->storage, addr, &value_size);
            if (!value) {
                snprintf(db->last_error, sizeof(db->last_error),
                         "foreach: failed to read value");
                result = STARK_ERROR;
                break;
            }

            if (cb(key, value, value_size, user) != 0) break;

            // The callback may have written to the database (stale cursor) or
            // closed it (cursor->db becomes NULL); do not touch db after this.
            stark_result_t post = cursor_precheck(cursor);
            if (post != STARK_OK) {
                if (post == STARK_STALE_CURSOR) {
                    if (cursor->db) {
                        snprintf(cursor->db->last_error,
                                 sizeof(cursor->db->last_error),
                                 "foreach: modified during iteration");
                    }
                    post = STARK_ERROR;
                }
                result = post;
                break;
            }
        }

        stark_result_t next = stark_cursor_next(cursor);
        if (next == STARK_NOT_FOUND) break;
        if (next != STARK_OK) {
            if (next == STARK_STALE_CURSOR) {
                if (cursor->db) {
                    snprintf(cursor->db->last_error,
                             sizeof(cursor->db->last_error),
                             "foreach: modified during iteration");
                }
                next = STARK_ERROR;
            }
            result = next;
            break;
        }
    }

    stark_cursor_destroy(cursor);
    return result;
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
    if (reject_readonly(db)) return STARK_ERROR;
    if (!data && count > 0) return STARK_INVALID_ARG;
    db_touch(db);

    if (elem_size == 0) {
        elem_size = dtype_size(dtype);
        if (elem_size == 0) return STARK_INVALID_ARG;
    }

    data_addr_t addr;
    DB_Result result = db_insert_binary(db->internal_db, key, (uint8_t)dtype,
                                        data, elem_size, count, &addr);
    if (result != DB_SUCCESS) {
        snprintf(db->last_error, sizeof(db->last_error),
                 "failed to store binary array (error %d)", result);
        return STARK_ERROR;
    }
    return STARK_OK;
}

STARK_API stark_result_t stark_load_binary(stark_db_t* db, uint32_t key,
                                           stark_dtype_t* dtype,
                                           void* buffer,
                                           size_t* count,
                                           size_t* elem_size) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (!count) return STARK_INVALID_ARG;

    uint8_t dt = 0;
    uint16_t bad_version = 0;
    DB_Result result = db_find_binary(db->internal_db, key, buffer, count, elem_size,
                                      &dt, &bad_version);
    if (result != DB_SUCCESS) {
        if (result == DB_NOT_FOUND) return STARK_NOT_FOUND;
        if (result == DB_BUFFER_TOO_SMALL) {
            snprintf(db->last_error, sizeof(db->last_error),
                     "buffer too small: required %zu elements", *count);
            return STARK_BUFFER_TOO_SMALL;
        }
        if (bad_version) {
            snprintf(db->last_error, sizeof(db->last_error),
                     "unsupported binary header version %u", bad_version);
        }
        return STARK_ERROR;
    }

    if (dtype) *dtype = (stark_dtype_t)dt;
    return STARK_OK;
}

STARK_API stark_result_t stark_get_binary_ptr(stark_db_t* db, uint32_t key,
                                              stark_dtype_t* dtype,
                                              void** ptr,
                                              size_t* count,
                                              size_t* elem_size) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (!ptr) return STARK_INVALID_ARG;

    uint8_t dt = 0;
    uint16_t bad_version = 0;
    DB_Result result = db_get_binary_ptr(db->internal_db, key, ptr, count, elem_size,
                                         &dt, &bad_version);
    if (result != DB_SUCCESS) {
        if (result == DB_NOT_FOUND) return STARK_NOT_FOUND;
        if (bad_version) {
            snprintf(db->last_error, sizeof(db->last_error),
                     "unsupported binary header version %u", bad_version);
        }
        return STARK_ERROR;
    }

    if (dtype) *dtype = (stark_dtype_t)dt;
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
    if (reject_readonly(db)) return STARK_ERROR;
    if (!column_key || !schema_json || !columns || !column_sizes) return STARK_INVALID_ARG;
    db_touch(db);

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
    if (reject_readonly(db)) return STARK_ERROR;
    if (!key || !value || value_size == 0) return STARK_INVALID_ARG;
    db_touch(db);
    uint32_t hashed_key = hash_string(key);
    return stark_add(db, hashed_key, value, value_size);
}

STARK_API stark_result_t stark_get_str(stark_db_t* db, const char* key,
                                        void* buffer, size_t* buffer_size) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (!key || !buffer_size) return STARK_INVALID_ARG;
    uint32_t hashed_key = hash_string(key);
    return stark_get(db, hashed_key, buffer, buffer_size);
}

STARK_API stark_result_t stark_del_str(stark_db_t* db, const char* key) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (reject_readonly(db)) return STARK_ERROR;
    if (!key) return STARK_INVALID_ARG;
    db_touch(db);
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
    if (reject_readonly(db)) return STARK_ERROR;
    if (!batch || !batch->keys || !batch->values || !batch->sizes || batch->count == 0)
        return STARK_INVALID_ARG;
    db_touch(db);

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
    stats->btree_height = btree_height(internal->index);
    stats->data_size = (uint64_t)internal->storage->current_page * PAGE_SIZE
                       + internal->storage->current_offset;

    return STARK_OK;
}

STARK_API const char* stark_error(stark_db_t* db) {
    if (!db) return "Database handle is NULL";
    return db->last_error;
}

// Internal helper so lower layers (e.g. the type system) can attach warnings.
void stark_set_error_msg(stark_db_t* db, const char* msg) {
    if (!db || !msg) return;
    snprintf(db->last_error, sizeof(db->last_error), "%s", msg);
}

STARK_API stark_result_t stark_sync(stark_db_t* db) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    Database* internal = db->internal_db;
    if (internal->index && internal->index->pager) {
        pager_flush_all(internal->index->pager);
    }
    if (internal->storage && internal->storage->pager) {
        pager_flush_all(internal->storage->pager);
    }
    return STARK_OK;
}

STARK_API stark_result_t stark_compact(stark_db_t* db) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (reject_readonly(db)) return STARK_ERROR;
    if (db->in_transaction) {
        snprintf(db->last_error, sizeof(db->last_error),
                 "cannot compact during a transaction");
        return STARK_ERROR;
    }
    db_touch(db);

    if (stark_sync(db) != STARK_OK) {
        snprintf(db->last_error, sizeof(db->last_error), "failed to sync before compaction");
        return STARK_ERROR;
    }

    int new_lock = -1;
    DB_Result result = db_compact(db->internal_db, &new_lock);
    if (result != DB_SUCCESS) {
        if (new_lock >= 0) pager_release_lock(new_lock);
        snprintf(db->last_error, sizeof(db->last_error),
                 "compaction failed (error %d)", result);
        return (result == DB_FULL) ? STARK_FULL
             : (result == DB_MEMORY_ERROR) ? STARK_MEMORY_ERROR
             : (result == DB_IO_ERROR) ? STARK_IO_ERROR : STARK_ERROR;
    }

    // Hand the cross-process lock over to the newly installed .idx inode.
    if (new_lock >= 0) {
        pager_release_lock(db->lock_fd);
        db->lock_fd = new_lock;
    }
    return STARK_OK;
}

STARK_API stark_result_t stark_compact_stats(stark_db_t* db,
                                             uint64_t* before_bytes,
                                             uint64_t* after_bytes) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (reject_readonly(db)) return STARK_ERROR;

    // Flush first so the "before" size reflects all pending writes.
    if (!db->in_transaction) stark_sync(db);

    uint64_t before = pager_file_size(db->internal_db->index->pager) +
                      pager_file_size(db->internal_db->storage->pager);

    stark_result_t result = stark_compact(db);
    if (result != STARK_OK) return result;

    uint64_t after = pager_file_size(db->internal_db->index->pager) +
                     pager_file_size(db->internal_db->storage->pager);
    if (before_bytes) *before_bytes = before;
    if (after_bytes) *after_bytes = after;
    return STARK_OK;
}

// ==================== TYPE SYSTEM IMPLEMENTATION ====================

extern stark_result_t type_create(stark_db_t* db, const char* name,
                                  FieldDef* fields, uint32_t field_count);
extern TypeDef* type_get(stark_db_t* db, const char* name);
extern stark_result_t type_delete(stark_db_t* db, const char* name);
extern stark_result_t type_manifest_update(stark_db_t* db, const char* type_name,
                                           const char* record_key);
extern stark_result_t type_list(stark_db_t* db, char*** names, uint32_t* count);
extern stark_result_t type_serialize(FieldDef* fields, uint32_t field_count,
                                     const char* field_values, void* buffer);
extern stark_result_t type_deserialize(FieldDef* fields, uint32_t field_count,
                                       const void* buffer, char* output, size_t output_size);
extern stark_result_t type_deserialize_ex(FieldDef* fields, uint32_t field_count,
                                          const void* buffer, char* output,
                                          size_t output_size, size_t* required);

STARK_API stark_result_t stark_define_type(stark_db_t* db, const char* name,
                                           FieldDef* fields, uint32_t field_count) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (reject_readonly(db)) return STARK_ERROR;
    if (!name || !fields || field_count == 0) return STARK_INVALID_ARG;
    return type_create(db, name, fields, field_count);
}

STARK_API stark_result_t stark_undefine_type(stark_db_t* db, const char* name) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (reject_readonly(db)) return STARK_ERROR;
    if (!name) return STARK_INVALID_ARG;
    db_touch(db);
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
    if (reject_readonly(db)) return STARK_ERROR;
    if (!type_name || !field_values) return STARK_INVALID_ARG;
    db_touch(db);

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
        if (result == STARK_OK) {
            // Track this record so undefine_type can remove it later.
            type_manifest_update(db, type_name, data_key);
        }
    }

    free(buffer);
    free(type);
    return result;
}

STARK_API stark_result_t stark_get_typed(stark_db_t* db, const char* type_name,
                                         uint32_t key, char* output, size_t output_size) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (!type_name || !output) return STARK_INVALID_ARG;

    TypeDef* type = type_get(db, type_name);
    if (!type) {
        return STARK_NOT_FOUND;
    }

    char data_key[256];
    snprintf(data_key, sizeof(data_key), "%s:%u", type_name, key);

    // Probe the stored record size first: the type definition may have been
    // written by an older version with a different record size.
    size_t size = 0;
    stark_result_t result = stark_get_str(db, data_key, NULL, &size);
    if (result == STARK_NOT_FOUND) {
        free(type);
        return STARK_NOT_FOUND;
    }
    if (result != STARK_BUFFER_TOO_SMALL) {
        free(type);
        return STARK_ERROR;
    }

    void* buffer = malloc(size);
    if (!buffer) {
        free(type);
        return STARK_MEMORY_ERROR;
    }

    size_t got = size;
    result = stark_get_str(db, data_key, buffer, &got);
    if (result == STARK_OK) {
        size_t required = 0;
        result = type_deserialize_ex(type->fields, type->field_count,
                                     buffer, output, output_size, &required);
        if (result == STARK_BUFFER_TOO_SMALL) {
            snprintf(db->last_error, sizeof(db->last_error),
                     "buffer too small: required %zu bytes", required);
        }
    }

    free(buffer);
    free(type);
    return result;
}

// ==================== TRANSACTIONS ====================

// Transactions use a copy-truncate scheme. Because page flushes overwrite
// existing pages in place, all flushes are suppressed while a transaction is
// active so the on-disk files remain byte-identical to their state at begin().
// Committing flushes the accumulated changes; rolling back truncates both
// files to their captured sizes, discards the page cache and re-initializes
// the B-tree / storage from disk.

STARK_API stark_result_t stark_begin(stark_db_t* db) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (reject_readonly(db)) return STARK_ERROR;
    if (db->in_transaction) {
        snprintf(db->last_error, sizeof(db->last_error),
                 "transaction already active");
        return STARK_ERROR;
    }

    Database* internal = db->internal_db;

    // Make the pre-transaction state durable so it survives a crash.
    if (stark_sync(db) != STARK_OK) {
        snprintf(db->last_error, sizeof(db->last_error), "failed to sync before begin");
        return STARK_ERROR;
    }
    pager_fsync(internal->index->pager);
    pager_fsync(internal->storage->pager);

    db->txn_idx_size = pager_file_size(internal->index->pager);
    db->txn_dat_size = pager_file_size(internal->storage->pager);

    if (write_txn_sentinel(db) != 0) {
        snprintf(db->last_error, sizeof(db->last_error),
                 "failed to write transaction sentinel");
        return STARK_ERROR;
    }

    // Suppress flushes for the duration of the transaction.
    pager_set_flush_suppressed(internal->index->pager, 1);
    pager_set_flush_suppressed(internal->storage->pager, 1);
    db->in_transaction = 1;
    return STARK_OK;
}

STARK_API stark_result_t stark_commit(stark_db_t* db) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (!db->in_transaction) {
        snprintf(db->last_error, sizeof(db->last_error), "no active transaction");
        return STARK_ERROR;
    }

    Database* internal = db->internal_db;
    pager_set_flush_suppressed(internal->index->pager, 0);
    pager_set_flush_suppressed(internal->storage->pager, 0);

    db->in_transaction = 0;
    db->txn_idx_size = 0;
    db->txn_dat_size = 0;

    stark_result_t result = stark_sync(db);
    pager_fsync(internal->index->pager);
    pager_fsync(internal->storage->pager);
    delete_txn_sentinel(db->path);

    if (result != STARK_OK) {
        snprintf(db->last_error, sizeof(db->last_error), "failed to commit transaction");
        return STARK_ERROR;
    }
    return STARK_OK;
}

STARK_API stark_result_t stark_rollback(stark_db_t* db) {
    if (!db || !db->internal_db) return STARK_CLOSED;
    if (!db->in_transaction) {
        snprintf(db->last_error, sizeof(db->last_error), "no active transaction");
        return STARK_ERROR;
    }

    Database* internal = db->internal_db;
    pager_set_flush_suppressed(internal->index->pager, 0);
    pager_set_flush_suppressed(internal->storage->pager, 0);

    // Rollback rebuilds the tree, so any existing cursor must be invalidated
    // (bump the counter even for a zero-write transaction).
    db_touch(db);

    // Discard all cached (uncommitted) pages, then truncate the files back to
    // their captured sizes. Nothing was flushed, so this restores pre-begin.
    if (pager_truncate(internal->index->pager, db->txn_idx_size) != DB_SUCCESS ||
        pager_truncate(internal->storage->pager, db->txn_dat_size) != DB_SUCCESS) {
        snprintf(db->last_error, sizeof(db->last_error), "failed to truncate on rollback");
        return STARK_ERROR;
    }
    pager_invalidate_all(internal->index->pager);
    pager_invalidate_all(internal->storage->pager);

    if (db_reinit(internal) != DB_SUCCESS) {
        snprintf(db->last_error, sizeof(db->last_error), "failed to reinitialize after rollback");
        return STARK_ERROR;
    }

    db->in_transaction = 0;
    db->txn_idx_size = 0;
    db->txn_dat_size = 0;
    delete_txn_sentinel(db->path);
    return STARK_OK;
}

STARK_API int stark_in_transaction(stark_db_t* db) {
    if (!db) return 0;
    return db->in_transaction;
}
