#include <stark.hpp>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

static void remove_db(const char* p) {
    const char* suffixes[] = { ".idx", ".dat" };
    char path[512];
    for (const char* s : suffixes) {
        snprintf(path, sizeof(path), "%s%s", p, s);
        remove(path);
    }
}

static std::string read_type_blob(const char* path, const char* name) {
    stark_db_t* db = stark_open(path, 0);
    if (!db) return "<openfail>";
    char key[256];
    snprintf(key, sizeof(key), "type:%s", name);
    size_t sz = 0;
    stark_get_str(db, key, NULL, &sz);
    std::string out;
    out.resize(sz);
    if (sz) {
        size_t s2 = sz;
        if (stark_get_str(db, key, &out[0], &s2) == STARK_OK) out.resize(s2);
    }
    stark_close(db);
    return out;
}

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            printf("  FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__);      \
            return 1;                                                       \
        }                                                                   \
    } while (0)

int main() {
    const char* a = "/tmp/stark_test_cpp_types_a";
    const char* b = "/tmp/stark_test_cpp_types_b";
    remove_db(a);
    remove_db(b);

    // C++ multi-field definition and round-trip.
    {
        stark::Database db(a);
        db.define_type("player", {
            stark::Field("a", TYPE_INT),
            stark::Field("b", TYPE_INT),
            stark::Field("c", TYPE_STRING, 16),
        });
        db.add_typed("player", 1, "a=100 b=50 c=hello");

        std::string got = db.get_typed("player", 1);
        CHECK(got == "a=100 b=50 c=\"hello\"", "multi-field round-trip");

        int caught = 0;
        try { db.define_type("empty", {}); } catch (const stark::Error&) { caught++; }
        try { db.define_type("badstr", { stark::Field("x", TYPE_STRING, 0) }); }
        catch (const stark::Error&) { caught++; }
        try { db.define_type("dup", { stark::Field("x", TYPE_INT), stark::Field("x", TYPE_INT) }); }
        catch (const stark::Error&) { caught++; }
        CHECK(caught == 3, "validation rejects invalid type specs");
    }

    // Raw C definition emulating the Python layout (explicit offsets, int=4).
    {
        stark_db_t* db = stark_open(b, 0);
        CHECK(db != NULL, "open raw db");
        FieldDef f[3];
        memset(f, 0, sizeof(f));
        strcpy(f[0].name, "a"); f[0].type = TYPE_INT;    f[0].size = 4;  f[0].offset = 0;
        strcpy(f[1].name, "b"); f[1].type = TYPE_INT;    f[1].size = 4;  f[1].offset = 4;
        strcpy(f[2].name, "c"); f[2].type = TYPE_STRING; f[2].size = 16; f[2].offset = 8;
        CHECK(stark_define_type(db, "player", f, 3) == STARK_OK, "raw define");
        stark_close(db);
    }

    // Byte-identical on-disk type definitions.
    std::string cpp_blob = read_type_blob(a, "player");
    std::string raw_blob = read_type_blob(b, "player");
    CHECK(cpp_blob.size() == raw_blob.size(), "type blob size matches");
    CHECK(memcmp(cpp_blob.data(), raw_blob.data(), cpp_blob.size()) == 0,
          "type blob byte-identical");

    // Cross-binding interoperability.
    {
        stark_db_t* db = stark_open(b, 0);
        CHECK(db != NULL, "open raw db 2");
        CHECK(stark_add_typed(db, "player", 2, "a=7 b=8 c=xyz") == STARK_OK, "raw add");
        stark_close(db);

        stark::Database cppdb(b);
        CHECK(cppdb.get_typed("player", 2) == "a=7 b=8 c=\"xyz\"", "C++ reads raw record");
    }

    const char* c = "/tmp/stark_test_cpp_types_c";
    const char* bak = "/tmp/stark_test_cpp_types_bak";
    remove_db(c);
    remove_db(bak);

    {
        stark::Database db(c);

        // Retry-on-buffer-too-small must handle values far above 4096 bytes.
        std::string large(60000, 'x');
        db.add(1, large);
        std::string got_num = db.get(1);
        CHECK(got_num.size() >= large.size() &&
              got_num.compare(0, large.size(), large) == 0,
              "C++ large numeric get");
        db.put_str("large", large);
        std::string got_str = db.get_str("large");
        CHECK(got_str.size() >= large.size() &&
              got_str.compare(0, large.size(), large) == 0,
              "C++ large string get");

        // Bounded foreach_range.
        for (uint32_t i = 0; i < 4; i++) {
            db.add(1000000u + i, std::string("v") + std::to_string(i));
        }
        int seen = 0;
        uint32_t last = 0;
        db.foreach_range(1000000u, 2000000u,
                         [&](uint32_t key, const void*, size_t) {
                             if (seen > 0 && key < last) seen = -1000;
                             last = key;
                             seen++;
                         });
        CHECK(seen == 4, "C++ foreach_range count");

        db.backup(bak);
        int locked = 0;
        try { stark::Database second(c); } catch (const stark::Error&) { locked++; }
        CHECK(locked == 1, "C++ locked constructor throws");
    }

    {
        stark::Database db(bak);
        std::string got = db.get(1);
        CHECK(got.size() >= 60000 && got.compare(0, 60000, std::string(60000, 'x')) == 0,
              "C++ backup contents");
    }

    remove_db(c);
    remove_db(bak);
    printf("  C++ type interop tests passed\n");
    return 0;
}
