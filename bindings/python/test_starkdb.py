#!/usr/bin/env python3
"""Comprehensive test of STARKDB Python bindings and ML features."""
import sys
import os
import shutil

# Add build dir to LD_LIBRARY_PATH for the test
os.environ["STARK_LIB_PATH"] = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "build", "libstark.so"
)

# Clean test data
test_dir = "/tmp/starkdb_test"
shutil.rmtree(test_dir, ignore_errors=True)
os.makedirs(test_dir, exist_ok=True)

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "bindings", "python"))

from starkdb import Database, StarkDBNotFound, StarkDBError

TEST_DB = os.path.join(test_dir, "testdb")

def test_basic_crud():
    print("=" * 60)
    print("TEST 1: Basic CRUD Operations")
    print("=" * 60)

    db = Database(TEST_DB)

    # Put
    db.put(1, b"Hello World")
    db.put(2, b"STARK DB")
    assert db.exists(1), "Key 1 should exist"
    assert db.exists(2), "Key 2 should exist"
    assert not db.exists(999), "Key 999 should not exist"
    print("PASS: put and exists")

    # Get
    val = db.get(1)
    assert val == b"Hello World", f"Expected b'Hello World', got {val}"
    val = db.get_text(1)
    assert val == "Hello World", f"Expected 'Hello World', got {val}"
    print("PASS: get")

    # Dict-like access
    db[3] = b"Dict access"
    assert db[3] == b"Dict access"
    assert 3 in db
    del db[3]
    assert 3 not in db
    print("PASS: dict-like access (__getitem__, __setitem__, __delitem__, __contains__)")

    # Delete
    db.delete(2)
    assert not db.exists(2), "Key 2 should be deleted"
    try:
        db.get(2)
        assert False, "Should raise StarkDBNotFound"
    except StarkDBNotFound:
        pass
    print("PASS: delete with exception")

    # String keys
    db.put_str("player_name", "Hero")
    db.put_str("player_hp", "100")
    assert db.exists_str("player_name"), "String key should exist"
    val = db.get_str_text("player_name")
    assert val == "Hero", f"Expected 'Hero', got {val}"
    db.delete_str("player_name")
    assert not db.exists_str("player_name"), "String key should be deleted"
    print("PASS: string key operations")

    db.close()
    print("TEST 1: ALL PASSED\n")


def test_iteration():
    print("=" * 60)
    print("TEST 2: Iteration / Cursor")
    print("=" * 60)

    db = Database(TEST_DB)
    # Clean up from previous test
    for k in range(1, 20):
        try:
            db.delete(k)
        except StarkDBNotFound:
            pass
    db.sync()

    # Insert test data
    for i in range(10):
        db.put(i, f"Value_{i}".encode())

    keys = list(iter(db))
    assert len(keys) == 10, f"Expected 10 keys, got {len(keys)}"
    assert sorted(keys) == keys, f"Keys should be sorted, got {keys}"
    print(f"PASS: iteration yields {len(keys)} sorted keys: {keys}")

    # Items iteration
    items = list(db.items())
    assert len(items) == 10, f"Expected 10 items, got {len(items)}"
    for k, v in items:
        assert v.startswith(b"Value_"), f"Unexpected value: {v}"
    print(f"PASS: items() iteration works")

    # Clean up
    for i in range(10):
        db.delete(i)
    db.close()
    print("TEST 2: ALL PASSED\n")


def test_binary_array():
    print("=" * 60)
    print("TEST 3: Binary Array Storage (ML Feature)")
    print("=" * 60)

    try:
        import numpy as np
    except ImportError:
        print("SKIP: numpy not installed")
        return

    db = Database(TEST_DB)

    # Store float32 array
    original = np.array([1.0, 2.5, 3.7, 4.2, 5.9], dtype=np.float32)
    db.store_array(100, original)
    loaded = db.load_array(100)
    assert np.allclose(original, loaded), f"Arrays don't match: {original} vs {loaded}"
    print(f"PASS: float32 array stored/loaded: {loaded}")

    # Store int32 array
    original2 = np.array([10, 20, 30, 40, 50], dtype=np.int32)
    db.store_array(101, original2)
    loaded2 = db.load_array(101)
    assert np.array_equal(original2, loaded2), f"Arrays don't match: {original2} vs {loaded2}"
    print(f"PASS: int32 array stored/loaded: {loaded2}")

    # Zero-copy
    loaded_zc = db.load_array_zerocopy(100)
    assert np.allclose(original, loaded_zc), "Zero-copy array mismatch"
    print(f"PASS: zero-copy access works, dtype={loaded_zc.dtype}, shape={loaded_zc.shape}")

    # Large array test
    large = np.random.randn(1000).astype(np.float32)
    db.store_array(200, large)
    large_loaded = db.load_array(200)
    assert np.allclose(large, large_loaded), "Large array mismatch"
    print(f"PASS: large array (1000 floats) stored/loaded correctly")

    db.close()
    print("TEST 3: ALL PASSED\n")


def test_dataframe():
    print("=" * 60)
    print("TEST 4: Arrow Column Layout / DataFrame")
    print("=" * 60)

    try:
        import pandas as pd
        import numpy as np
    except ImportError:
        print("SKIP: pandas not installed")
        return

    db = Database(TEST_DB)

    # Create a DataFrame
    df = pd.DataFrame({
        "name": ["Alice", "Bob", "Charlie", "Diana"],
        "age": np.array([25, 30, 35, 28], dtype=np.int32),
        "score": np.array([95.5, 87.2, 91.8, 78.9], dtype=np.float32),
    })

    db.store_dataframe("users", df)
    loaded_df = db.load_dataframe("users")
    print(f"PASS: DataFrame stored/loaded: {loaded_df.shape[0]} rows x {loaded_df.shape[1]} cols")
    print(f"      Columns: {list(loaded_df.columns)}")

    assert loaded_df.shape[0] == 4, f"Expected 4 rows, got {loaded_df.shape[0]}"
    assert list(loaded_df["name"]) == ["Alice", "Bob", "Charlie", "Diana"]

    db.close()
    print("TEST 4: ALL PASSED\n")


def test_batch_and_transactions():
    print("=" * 60)
    print("TEST 5: Batch Operations & Transactions")
    print("=" * 60)

    db = Database(TEST_DB)

    # Batch put
    db.put_batch({10: b"A", 11: b"B", 12: b"C"})
    assert db.exists(10) and db.exists(11) and db.exists(12)
    print("PASS: batch put")

    # Batch get
    results = db.get_batch([10, 11, 12, 999])
    assert len(results) == 3, f"Expected 3 results, got {len(results)}"
    assert results[10] == b"A"
    print("PASS: batch get")

    # Clean up
    for k in [10, 11, 12]:
        db.delete(k)

    db.close()
    print("TEST 5: ALL PASSED\n")


def test_context_manager():
    print("=" * 60)
    print("TEST 6: Context Manager")
    print("=" * 60)

    with Database(TEST_DB) as db:
        db.put(50, b"Context test")
        assert db.exists(50)
        assert not db.closed
    assert db.closed
    print("PASS: context manager works, db auto-closed")

    print("TEST 6: ALL PASSED\n")


def test_stats():
    print("=" * 60)
    print("TEST 7: Statistics")
    print("=" * 60)

    db = Database(TEST_DB)
    stats = db.stats()
    assert "keys_count" in stats
    assert "data_size" in stats
    assert "page_count" in stats
    print(f"PASS: stats = {stats}")
    db.close()
    print("TEST 7: ALL PASSED\n")


def test_type_system():
    print("=" * 60)
    print("TEST 8: Type System")
    print("=" * 60)

    db = Database(TEST_DB)

    db.define_type("player", [
        {"name": "id", "type": 1, "size": 4},
        {"name": "hp", "type": 1, "size": 4},
        {"name": "level", "type": 1, "size": 4},
    ])

    db.add_typed("player", 1, "id=100 hp=50 level=10")
    result = db.get_typed("player", 1)
    assert "id=100" in result
    assert "hp=50" in result
    print(f"PASS: typed data: {result}")

    db.undefine_type("player")
    db.close()
    print("TEST 8: ALL PASSED\n")


if __name__ == "__main__":
    print()
    print("*" * 60)
    print("  STARKDB PYTHON BINDINGS - TESTS")
    print("*" * 60)
    print()

    try:
        test_basic_crud()
        test_iteration()
        test_binary_array()
        test_dataframe()
        test_batch_and_transactions()
        test_context_manager()
        test_stats()
        test_type_system()

        print("*" * 60)
        print("  ALL TESTS PASSED!")
        print("*" * 60)
    except Exception as e:
        print(f"\nTEST FAILED: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
    finally:
        shutil.rmtree(test_dir, ignore_errors=True)
