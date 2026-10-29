#include "test_common.h"
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

static int check_value(stark_db_t* db, uint32_t key, const char* expected) {
    char buf[128];
    size_t sz = sizeof(buf);
    if (stark_get(db, key, buf, &sz) != STARK_OK) return 0;
    size_t len = strlen(expected);
    if (sz == len) return memcmp(buf, expected, len) == 0;
    if (sz == len + 1) return memcmp(buf, expected, len) == 0 && buf[len] == '\0';
    return 0;
}

static int has_magic(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    char magic[8];
    int ok = (fread(magic, 1, 8, f) == 8 && memcmp(magic, "STARKDB\0", 8) == 0);
    fclose(f);
    return ok;
}

int test_backup(void) {
    const char* src = "/tmp/stark_test_backup_src";
    const char* bak = "/tmp/stark_test_backup_bak";
    const char* restored = "/tmp/stark_test_backup_restored";
    const char* dest = "/tmp/stark_test_backup_dest";
    const char* partial = "/tmp/stark_test_backup_partial";
    const char* dir = "/tmp/stark_test_backup_dir";
    t_remove_all(src);
    t_remove_all(bak);
    t_remove_all(restored);
    t_remove_all(dest);
    t_remove_all(partial);
    rmdir(dir);
    remove(dir);

    stark_db_t* db = stark_open(src, 0);
    T_ASSERT(db != NULL, "open source");
    T_ASSERT(stark_add(db, 1, "hello", 6) == STARK_OK, "add 1");
    T_ASSERT(stark_add(db, 2, "world", 6) == STARK_OK, "add 2");

    // Round-trip backup while the source is open.
    T_ASSERT(stark_backup(db, bak) == STARK_OK, "backup");
    T_ASSERT(t_file_exists("/tmp/stark_test_backup_bak.idx"), "backup idx");
    T_ASSERT(t_file_exists("/tmp/stark_test_backup_bak.dat"), "backup dat");
    T_ASSERT(has_magic("/tmp/stark_test_backup_bak.idx"), "backup idx header");
    T_ASSERT(has_magic("/tmp/stark_test_backup_bak.dat"), "backup dat header");

    stark_db_t* bdb = stark_open(bak, 0);
    T_ASSERT(bdb != NULL, "open backup");
    T_ASSERT(check_value(bdb, 1, "hello") && check_value(bdb, 2, "world"),
             "backup contents");
    stark_close(bdb);

    // Restore to a fresh prefix.
    T_ASSERT(stark_restore(bak, restored) == STARK_OK, "restore");
    stark_db_t* rdb = stark_open(restored, 0);
    T_ASSERT(rdb != NULL, "open restored");
    T_ASSERT(check_value(rdb, 1, "hello") && check_value(rdb, 2, "world"),
             "restored contents");
    stark_close(rdb);

    // A locked destination is rejected and left unchanged.
    stark_db_t* destdb = stark_open(dest, 0);
    T_ASSERT(destdb != NULL, "open dest");
    T_ASSERT(stark_add(destdb, 9, "keep", 5) == STARK_OK, "add dest key");
    T_ASSERT(stark_restore(bak, dest) == STARK_ERROR, "restore onto locked dest");
    T_ASSERT(check_value(destdb, 9, "keep"), "locked dest unchanged");
    stark_close(destdb);
    t_remove_all(dest);

    // Existing valid unlocked destination is replaced.
    destdb = stark_open(dest, 0);
    T_ASSERT(destdb != NULL, "reopen dest");
    T_ASSERT(stark_add(destdb, 9, "old", 4) == STARK_OK, "old dest key");
    stark_close(destdb);
    T_ASSERT(stark_restore(bak, dest) == STARK_OK, "restore replaces dest");
    destdb = stark_open(dest, 0);
    T_ASSERT(destdb != NULL, "open replaced dest");
    T_ASSERT(check_value(destdb, 1, "hello"), "replacement key");
    T_ASSERT(!stark_exists(destdb, 9), "old key gone");
    stark_close(destdb);
    t_remove_all(dest);

    // Stale temp files are cleaned and the operation succeeds in the same call.
    // First simulate a crash between the two renames (only the dat temp left).
    {
        FILE* f = fopen("/tmp/stark_test_backup_bak.dat.tmp", "wb");
        T_ASSERT(f != NULL, "create dat tmp");
        fputs("junk", f);
        fclose(f);
    }
    T_ASSERT(stark_backup(db, bak) == STARK_OK, "backup after half rename");
    T_ASSERT(!t_file_exists("/tmp/stark_test_backup_bak.dat.tmp"), "half dat tmp gone");

    {
        FILE* f = fopen("/tmp/stark_test_backup_bak.idx.tmp", "wb");
        T_ASSERT(f != NULL, "create idx tmp");
        fputs("junk", f);
        fclose(f);
        f = fopen("/tmp/stark_test_backup_bak.dat.tmp", "wb");
        T_ASSERT(f != NULL, "create dat tmp");
        fputs("junk", f);
        fclose(f);
    }
    T_ASSERT(stark_backup(db, bak) == STARK_OK, "backup with stale temps");
    T_ASSERT(!t_file_exists("/tmp/stark_test_backup_bak.idx.tmp"), "idx tmp gone");
    T_ASSERT(!t_file_exists("/tmp/stark_test_backup_bak.dat.tmp"), "dat tmp gone");

    // Backup during a transaction is rejected.
    T_ASSERT(stark_begin(db) == STARK_OK, "begin");
    T_ASSERT(stark_backup(db, "/tmp/stark_test_backup_txn") == STARK_ERROR,
             "backup during txn");
    T_ASSERT(strstr(stark_error(db), "transaction") != NULL, "txn message");
    T_ASSERT(stark_rollback(db) == STARK_OK, "rollback");
    t_remove_all("/tmp/stark_test_backup_txn");

    // Incomplete source.
    T_ASSERT(t_copy_file("/tmp/stark_test_backup_bak.idx",
                         "/tmp/stark_test_backup_partial.idx") == 0,
             "copy partial idx");
    T_ASSERT(stark_restore(partial, "/tmp/stark_test_backup_out") == STARK_ERROR,
             "incomplete source rejected");
    t_remove_all(partial);
    t_remove_all("/tmp/stark_test_backup_out");

    // Directory destination.
    T_ASSERT(mkdir(dir, 0777) == 0, "mkdir dest");
    T_ASSERT(stark_backup(db, dir) == STARK_IO_ERROR, "directory dest rejected");
    rmdir(dir);

    // Empty database backup.
    {
        const char* empty = "/tmp/stark_test_backup_empty";
        const char* empty_bak = "/tmp/stark_test_backup_empty_bak";
        t_remove_all(empty);
        t_remove_all(empty_bak);
        stark_db_t* edb = stark_open(empty, 0);
        T_ASSERT(edb != NULL, "open empty db");
        T_ASSERT(stark_backup(edb, empty_bak) == STARK_OK, "backup empty db");
        stark_close(edb);
        T_ASSERT(t_file_exists("/tmp/stark_test_backup_empty_bak.idx"), "empty idx copy");
        T_ASSERT(t_file_exists("/tmp/stark_test_backup_empty_bak.dat"), "empty dat copy");
        t_remove_all(empty);
        t_remove_all(empty_bak);
    }

    stark_close(db);
    t_remove_all(src);
    t_remove_all(bak);
    t_remove_all(restored);
    t_remove_all(dest);
    t_remove_all(partial);
    printf("  backup tests passed\n");
    return 0;
}
