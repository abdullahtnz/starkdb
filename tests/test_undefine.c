#include "test_common.h"

static void define_int_type(stark_db_t* db, const char* name) {
    FieldDef f;
    memset(&f, 0, sizeof(f));
    strcpy(f.name, "a");
    f.type = TYPE_INT;
    f.size = 4;
    f.offset = 0;
    stark_define_type(db, name, &f, 1);
}

static int manifest_lines(stark_db_t* db, const char* manifest) {
    char b[4096];
    size_t s = sizeof(b);
    if (stark_get_str(db, manifest, b, &s) != STARK_OK) return -1;
    int n = 0;
    size_t start = 0;
    for (size_t i = 0; i <= s; i++) {
        if (i == s || b[i] == '\n') {
            if (i > start) n++;
            start = i + 1;
        }
    }
    return n;
}

int test_undefine(void) {
    const char* p = "/tmp/stark_test_undefine";
    t_remove_all(p);

    stark_db_t* db = stark_open(p, 0);
    T_ASSERT(db != NULL, "open");

    define_int_type(db, "player");
    T_ASSERT(t_str_exists(db, "__type_manifest__:player"), "manifest created at define");
    T_ASSERT(stark_add_typed(db, "player", 1, "a=10") == STARK_OK, "add 1");
    T_ASSERT(stark_add_typed(db, "player", 2, "a=20") == STARK_OK, "add 2");
    T_ASSERT(stark_add_typed(db, "player", 3, "a=30") == STARK_OK, "add 3");
    T_ASSERT(t_str_exists(db, "player:1") && t_str_exists(db, "player:2") &&
             t_str_exists(db, "player:3"), "records exist");
    T_ASSERT(manifest_lines(db, "__type_manifest__:player") == 3, "manifest 3 entries");

    // Re-adding the same record keeps one manifest entry.
    T_ASSERT(stark_add_typed(db, "player", 1, "a=11") == STARK_OK, "re-add");
    T_ASSERT(manifest_lines(db, "__type_manifest__:player") == 3, "manifest still 3");

    // Undefine deletes records + definition + manifest.
    T_ASSERT(stark_undefine_type(db, "player") == STARK_OK, "undefine");
    T_ASSERT(!t_str_exists(db, "player:1") && !t_str_exists(db, "player:2") &&
             !t_str_exists(db, "player:3"), "records deleted");
    T_ASSERT(!t_str_exists(db, "type:player"), "type deleted");
    T_ASSERT(!t_str_exists(db, "__type_manifest__:player"), "manifest deleted");

    // Two types are isolated.
    define_int_type(db, "player");
    define_int_type(db, "team");
    T_ASSERT(stark_add_typed(db, "player", 1, "a=1") == STARK_OK, "p add");
    T_ASSERT(stark_add_typed(db, "team", 1, "a=2") == STARK_OK, "t add");
    T_ASSERT(stark_undefine_type(db, "player") == STARK_OK, "undefine player");
    T_ASSERT(!t_str_exists(db, "player:1"), "player record gone");
    T_ASSERT(t_str_exists(db, "team:1") && t_str_exists(db, "type:team"), "team untouched");
    T_ASSERT(stark_undefine_type(db, "team") == STARK_OK, "undefine team");

    // Define/undefine/define same name: old records gone, new usable.
    define_int_type(db, "player");
    T_ASSERT(stark_add_typed(db, "player", 9, "a=9") == STARK_OK, "old add");
    T_ASSERT(stark_undefine_type(db, "player") == STARK_OK, "undefine again");
    define_int_type(db, "player");
    T_ASSERT(!t_str_exists(db, "player:9"), "old record gone after redefine");
    T_ASSERT(stark_add_typed(db, "player", 1, "a=1") == STARK_OK, "new add");
    T_ASSERT(t_str_exists(db, "player:1"), "new record works");

    // Manifest without trailing newline, listing a nonexistent key.
    {
        const char* m = "player:1\nplayer:55";
        T_ASSERT(stark_put_str(db, "__type_manifest__:player", m, strlen(m)) == STARK_OK,
                 "write weird manifest");
        T_ASSERT(stark_undefine_type(db, "player") == STARK_OK, "undefine weird manifest");
        T_ASSERT(!t_str_exists(db, "player:1"), "record deleted despite no trailing NL");
        T_ASSERT(!t_str_exists(db, "__type_manifest__:player"), "manifest gone");
    }

    // Undefining a nonexistent type is an error.
    T_ASSERT(stark_undefine_type(db, "nope") != STARK_OK, "nonexistent type errors");

    stark_close(db);
    t_remove_all(p);

    printf("  undefine tests passed\n");
    return 0;
}
