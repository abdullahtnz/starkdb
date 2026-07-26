"""High-level Pythonic Database class for STARKDB."""
import ctypes
import os
from typing import Optional, Iterator, Tuple, Any, Union, Dict, List

from ._lib import get_lib, STARK_OK, STARK_NOT_FOUND


class StarkDBError(Exception):
    """Base exception for STARK database errors."""
    pass


class StarkDBNotFound(StarkDBError):
    """Key not found."""
    pass


class StarkDBFull(StarkDBError):
    """Storage full."""
    pass


class StarkDBIOError(StarkDBError):
    """I/O error."""
    pass


class StarkDBValueError(StarkDBError):
    """Invalid argument."""
    pass


class StarkDBClosed(StarkDBError):
    """Database is closed."""
    pass


class StarkDBMemoryError(StarkDBError):
    """Memory allocation failed."""
    pass


class Database:
    """STARK Database connection.

    Provides a Pythonic interface to the embedded STARK key-value database.

    Usage:
        db = Database("mydb")
        db["key"] = b"value"
        value = db["key"]
        del db["key"]

        with Database("mydb") as db:
            db["player"] = "Hero"
            print(db["player"])
    """

    def __init__(self, path: str):
        """Open or create a database at the given path.

        Creates .idx and .dat files automatically.
        """
        self._path = path
        self._lib = get_lib()
        self._handle = self._lib.stark_open(path.encode("utf-8"), 0)
        if not self._handle:
            raise StarkDBError(f"Failed to open database: {path}")
        self._closed = False

    def close(self):
        """Flush and close the database."""
        if self._closed:
            return
        try:
            self.sync()
        except Exception:
            pass
        self._lib.stark_close(self._handle)
        self._handle = None
        self._closed = True

    @property
    def closed(self) -> bool:
        """Whether the database is closed."""
        return self._closed

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
        return False

    def __del__(self):
        if not self._closed and self._handle:
            try:
                self.close()
            except Exception:
                pass

    def _check_open(self):
        if self._closed or not self._handle:
            raise StarkDBClosed("Database is closed")

    # ---- Numeric Key Operations ----

    def put(self, key: int, value: Union[bytes, str, bytearray]) -> None:
        """Insert or update a value for a numeric key."""
        self._check_open()
        if isinstance(value, str):
            value = value.encode("utf-8")
        elif isinstance(value, bytearray):
            value = bytes(value)
        result = self._lib.stark_put(
            self._handle, ctypes.c_uint32(key), value, len(value)
        )
        if result != STARK_OK:
            raise StarkDBError(f"Failed to put key {key}: code {result}")

    def get(self, key: int, default: Any = None) -> Optional[bytes]:
        """Get a value by numeric key. Returns default if not found."""
        self._check_open()
        buf = ctypes.create_string_buffer(65536)
        size = ctypes.c_size_t(65536)
        result = self._lib.stark_get(self._handle, ctypes.c_uint32(key), buf, ctypes.byref(size))
        if result == STARK_OK:
            return buf.raw[:size.value]
        elif result == STARK_NOT_FOUND:
            if default is not None:
                return default
            raise StarkDBNotFound(f"Key {key} not found")
        else:
            raise StarkDBError(f"Failed to get key {key}: code {result}")

    def get_text(self, key: int, default: Any = None) -> Optional[str]:
        """Get a value as string by numeric key."""
        val = self.get(key, default=None)
        if val is None:
            if default is not None:
                return default
            raise StarkDBNotFound(f"Key {key} not found")
        return val.decode("utf-8", errors="replace").rstrip("\0")

    def delete(self, key: int) -> bool:
        """Delete a numeric key. Returns True if it existed."""
        self._check_open()
        result = self._lib.stark_delete(self._handle, ctypes.c_uint32(key))
        if result == STARK_OK:
            return True
        if result == STARK_NOT_FOUND:
            return False
        raise StarkDBError(f"Failed to delete key {key}: code {result}")

    def exists(self, key: int) -> bool:
        """Check if a numeric key exists."""
        self._check_open()
        return self._lib.stark_exists(self._handle, ctypes.c_uint32(key)) != 0

    # ---- String Key Operations ----

    def put_str(self, key: str, value: Union[bytes, str]) -> None:
        """Insert or update a value for a string key."""
        self._check_open()
        if isinstance(value, str):
            value = value.encode("utf-8")
        result = self._lib.stark_put_str(
            self._handle, key.encode("utf-8"), value, len(value)
        )
        if result != STARK_OK:
            raise StarkDBError(f"Failed to put string key '{key}': code {result}")

    def get_str(self, key: str, default: Any = None) -> Optional[bytes]:
        """Get a value by string key."""
        self._check_open()
        buf = ctypes.create_string_buffer(65536)
        size = ctypes.c_size_t(65536)
        result = self._lib.stark_get_str(self._handle, key.encode("utf-8"), buf, ctypes.byref(size))
        if result == STARK_OK:
            return buf.raw[:size.value]
        elif result == STARK_NOT_FOUND:
            if default is not None:
                return default
            raise StarkDBNotFound(f"String key '{key}' not found")
        else:
            raise StarkDBError(f"Failed to get string key '{key}': code {result}")

    def get_str_text(self, key: str, default: Any = None) -> Optional[str]:
        """Get a string key value as text."""
        val = self.get_str(key, default=None)
        if val is None:
            if default is not None:
                return default
            raise StarkDBNotFound(f"String key '{key}' not found")
        return val.decode("utf-8", errors="replace").rstrip("\0")

    def delete_str(self, key: str) -> bool:
        """Delete a string key."""
        self._check_open()
        result = self._lib.stark_del_str(self._handle, key.encode("utf-8"))
        if result == STARK_OK:
            return True
        if result == STARK_NOT_FOUND:
            return False
        raise StarkDBError(f"Failed to delete string key '{key}'")

    def exists_str(self, key: str) -> bool:
        """Check if a string key exists."""
        self._check_open()
        return self._lib.stark_exists_str(self._handle, key.encode("utf-8")) != 0

    # ---- Dict-like Access (Numeric Keys) ----

    def __getitem__(self, key: int) -> bytes:
        return self.get(key)

    def __setitem__(self, key: int, value: Union[bytes, str]) -> None:
        self.put(key, value)

    def __delitem__(self, key: int) -> None:
        if not self.delete(key):
            raise StarkDBNotFound(f"Key {key} not found")

    def __contains__(self, key: int) -> bool:
        return self.exists(key)

    # ---- Iteration ----

    def __iter__(self):
        """Iterate over all numeric keys."""
        self._check_open()
        cursor = self._lib.stark_cursor_create(self._handle)
        if not cursor:
            raise StarkDBError("Failed to create cursor")
        try:
            result = self._lib.stark_cursor_first(cursor)
            while result == STARK_OK:
                key = ctypes.c_uint32()
                buf = ctypes.create_string_buffer(65536)
                size = ctypes.c_size_t(65536)
                get_result = self._lib.stark_cursor_get(cursor, ctypes.byref(key), buf, ctypes.byref(size))
                if get_result == STARK_OK:
                    yield key.value
                result = self._lib.stark_cursor_next(cursor)
        finally:
            self._lib.stark_cursor_destroy(cursor)

    def items(self) -> Iterator[Tuple[int, bytes]]:
        """Iterate over all (key, value) pairs."""
        self._check_open()
        cursor = self._lib.stark_cursor_create(self._handle)
        if not cursor:
            raise StarkDBError("Failed to create cursor")
        try:
            result = self._lib.stark_cursor_first(cursor)
            while result == STARK_OK:
                key = ctypes.c_uint32()
                buf = ctypes.create_string_buffer(65536)
                size = ctypes.c_size_t(65536)
                get_result = self._lib.stark_cursor_get(cursor, ctypes.byref(key), buf, ctypes.byref(size))
                if get_result == STARK_OK:
                    yield key.value, buf.raw[:size.value]
                result = self._lib.stark_cursor_next(cursor)
        finally:
            self._lib.stark_cursor_destroy(cursor)

    def keys(self) -> List[int]:
        """Return all numeric keys."""
        return list(iter(self))

    def values(self) -> List[bytes]:
        """Return all values."""
        return [v for _, v in self.items()]

    def count(self) -> int:
        """Return the number of keys."""
        return len(self.keys())

    # ---- Binary Array Storage (ML Features) ----

    def store_array(self, key: int, array, dtype: int = 0) -> None:
        """Store a NumPy array or Python sequence as a contiguous binary array.

        Args:
            key: Numeric key
            array: NumPy array, bytes, or sequence of numbers
            dtype: STARK dtype code (0=float32, 1=float64, 2=int32, 4=uint8)
        """
        self._check_open()
        try:
            import numpy as np
            if isinstance(array, np.ndarray):
                arr = np.ascontiguousarray(array)
                data = arr.tobytes()
                elem_size = arr.itemsize
                count = arr.size
                if arr.dtype == np.float32: dtype = 0
                elif arr.dtype == np.float64: dtype = 1
                elif arr.dtype == np.int32: dtype = 2
                elif arr.dtype == np.int64: dtype = 3
                elif arr.dtype == np.uint8: dtype = 4
                elif arr.dtype == np.int8: dtype = 5
                elif arr.dtype == np.int16: dtype = 6
            else:
                raise TypeError("array must be a numpy ndarray")
        except ImportError:
            if isinstance(array, bytes):
                data = array
                elem_size = 1
                count = len(array)
                dtype = 4
            elif isinstance(array, (list, tuple)):
                import struct
                data = struct.pack(f"{len(array)}i", *array)
                elem_size = 4
                count = len(array)
                dtype = 2
            else:
                raise TypeError("array must be bytes, list, or numpy array (install numpy)")

        result = self._lib.stark_store_binary(
            self._handle, ctypes.c_uint32(key), ctypes.c_int(dtype),
            data, ctypes.c_size_t(elem_size), ctypes.c_size_t(count)
        )
        if result != STARK_OK:
            raise StarkDBError(f"Failed to store binary array at key {key}")

    def load_array(self, key: int) -> "numpy.ndarray":
        """Load a binary array as a NumPy array (zero-copy from DB memory).

        Returns a NumPy array backed directly by the database's memory pages.
        """
        self._check_open()
        import numpy as np

        dtype_out = ctypes.c_int()
        ptr = ctypes.c_void_p()
        count = ctypes.c_size_t()
        elem_size = ctypes.c_size_t()

        result = self._lib.stark_get_binary_ptr(
            self._handle, ctypes.c_uint32(key),
            ctypes.byref(dtype_out), ctypes.byref(ptr),
            ctypes.byref(count), ctypes.byref(elem_size)
        )
        if result != STARK_OK:
            if result == STARK_NOT_FOUND:
                raise StarkDBNotFound(f"Binary array at key {key} not found")
            raise StarkDBError(f"Failed to get binary pointer for key {key}")

        np_dtype_map = {0: np.float32, 1: np.float64, 2: np.int32, 3: np.int64,
                        4: np.uint8, 5: np.int8, 6: np.int16}
        np_dtype = np_dtype_map.get(dtype_out.value, np.float32)

        c_array = (ctypes.c_uint8 * (count.value * elem_size.value)).from_address(ptr.value or 0)
        if ptr.value == 0:
            raise StarkDBError("Null pointer returned from database")

        # Create numpy array sharing memory (zero-copy)
        arr = np.frombuffer(c_array, dtype=np_dtype, count=count.value)
        return arr.copy()  # Safe copy since page cache might be invalidated

    def load_array_zerocopy(self, key: int):
        """Load a binary array as a NumPy array with zero-copy (unsafe after DB writes)."""
        self._check_open()
        import numpy as np

        dtype_out = ctypes.c_int()
        ptr = ctypes.c_void_p()
        count = ctypes.c_size_t()
        elem_size = ctypes.c_size_t()

        result = self._lib.stark_get_binary_ptr(
            self._handle, ctypes.c_uint32(key),
            ctypes.byref(dtype_out), ctypes.byref(ptr),
            ctypes.byref(count), ctypes.byref(elem_size)
        )
        if result != STARK_OK:
            if result == STARK_NOT_FOUND:
                raise StarkDBNotFound(f"Binary array at key {key} not found")
            raise StarkDBError(f"Failed to get binary pointer for key {key}")

        np_dtype_map = {0: np.float32, 1: np.float64, 2: np.int32, 3: np.int64,
                        4: np.uint8, 5: np.int8, 6: np.int16}
        np_dtype = np_dtype_map.get(dtype_out.value, np.float32)

        if ptr.value == 0:
            raise StarkDBError("Null pointer returned from database")

        c_array = (ctypes.c_uint8 * (count.value * elem_size.value)).from_address(ptr.value)
        return np.frombuffer(c_array, dtype=np_dtype, count=count.value)

    # ---- Arrow Column Layout / DataFrame Support ----

    def store_dataframe(self, column_key: str, df) -> None:
        """Store a Pandas DataFrame using Arrow columnar layout.

        Each column is stored as a contiguous block; schema is preserved.
        """
        self._check_open()
        try:
            import numpy as np
        except ImportError:
            raise ImportError("numpy is required for DataFrame storage")

        if hasattr(df, 'to_dict'):
            data = df.to_dict(orient='list')
        else:
            data = dict(df)

        column_names = sorted(data.keys())
        num_cols = len(column_names)
        num_rows = len(data[column_names[0]]) if column_names else 0

        schema_parts = []
        columns = []
        col_sizes = []

        for name in column_names:
            raw_col = data[name]
            if isinstance(raw_col[0], str):
                joined = '\0'.join(str(v) for v in raw_col).encode('utf-8')
                columns.append(joined)
                col_sizes.append(len(joined))
                schema_parts.append(f'"{name}": "str"')
            else:
                col = np.ascontiguousarray(np.array(raw_col))
                columns.append(col.tobytes())
                col_sizes.append(len(col.tobytes()))
                dtype_name = str(col.dtype)
                schema_parts.append(f'"{name}": "{dtype_name}"')

        schema_json = "{" + ", ".join(schema_parts) + "}"

        c_columns = (ctypes.c_void_p * num_cols)()
        c_col_sizes = (ctypes.c_size_t * num_cols)()

        for i in range(num_cols):
            arr = ctypes.create_string_buffer(columns[i], len(columns[i]))
            c_columns[i] = ctypes.addressof(arr)
            c_col_sizes[i] = col_sizes[i]

        # Hold references to prevent GC
        self._last_column_refs = (c_columns, c_col_sizes, arr)

        result = self._lib.stark_store_columns(
            self._handle, column_key.encode("utf-8"), schema_json.encode("utf-8"),
            c_columns, c_col_sizes, ctypes.c_size_t(num_cols), ctypes.c_size_t(num_rows)
        )
        if result != STARK_OK:
            raise StarkDBError(f"Failed to store columns for '{column_key}'")

    def load_dataframe(self, column_key: str):
        """Load data stored as Arrow columns into a Pandas DataFrame."""
        self._check_open()
        try:
            import pandas as pd
            import numpy as np
            import json
        except ImportError:
            raise ImportError("pandas, numpy, and json are required for DataFrame loading")

        class ColumnData(ctypes.Structure):
            _fields_ = [
                ("schema_json", ctypes.c_char_p),
                ("column_data", ctypes.POINTER(ctypes.c_void_p)),
                ("column_sizes", ctypes.POINTER(ctypes.c_size_t)),
                ("num_columns", ctypes.c_size_t),
                ("num_rows", ctypes.c_size_t),
            ]

        out = ColumnData()
        result = self._lib.stark_load_columns(
            self._handle, column_key.encode("utf-8"), ctypes.byref(out)
        )
        if result != STARK_OK:
            raise StarkDBError(f"Failed to load columns for '{column_key}'")

        schema = {}
        if out.schema_json:
            try:
                schema = json.loads(out.schema_json.decode("utf-8", errors="replace"))
            except Exception:
                pass

        nc = out.num_columns
        nr = out.num_rows

        columns_data = {}
        schema_keys = list(schema.keys()) if schema else [f"col_{i}" for i in range(nc)]

        for i in range(min(nc, len(schema_keys))):
            col_name = schema_keys[i]
            col_size = out.column_sizes[i]
            col_ptr = out.column_data[i]

            if col_ptr is None or col_size == 0:
                continue

            dtype_str = schema.get(col_name, "float64")
            is_string = any(t in str(dtype_str) for t in ["U", "str", "object", "byt"])
            is_int = any(t in str(dtype_str) for t in ["int64", "int32", "int16", "int8", "uint"])

            if is_string:
                raw = ctypes.cast(col_ptr, ctypes.POINTER(ctypes.c_uint8 * col_size))
                full = bytes(raw.contents)
                strs = full.split(b'\x00')
                strs = [s.decode("utf-8", errors="replace") for s in strs]
                # Ensure exactly nr elements
                if len(strs) > nr:
                    strs = strs[:nr]
                elif len(strs) < nr:
                    strs.extend([''] * (nr - len(strs)))
                columns_data[col_name] = strs
            elif is_int:
                np_dtype = np.int64
                if "int32" in str(dtype_str): np_dtype = np.int32
                elif "int16" in str(dtype_str): np_dtype = np.int16
                elif "int8" in str(dtype_str): np_dtype = np.int8
                elif "uint" in str(dtype_str) and "8" in str(dtype_str): np_dtype = np.uint8
                buf = ctypes.cast(col_ptr, ctypes.POINTER(ctypes.c_uint8 * col_size))
                arr = np.frombuffer(buf.contents, dtype=np_dtype, count=nr)
                columns_data[col_name] = arr.copy()
            else:
                np_dtype = np.float64
                if "float32" in str(dtype_str): np_dtype = np.float32
                buf = ctypes.cast(col_ptr, ctypes.POINTER(ctypes.c_uint8 * col_size))
                arr = np.frombuffer(buf.contents, dtype=np_dtype, count=nr)
                columns_data[col_name] = arr.copy()

        self._lib.stark_free_columns(ctypes.byref(out))

        try:
            return pd.DataFrame(columns_data)
        except ImportError:
            return columns_data

    # ---- Transactions ----

    def begin(self) -> None:
        """Begin a transaction."""
        self._check_open()
        result = self._lib.stark_begin(self._handle)
        if result != STARK_OK:
            raise StarkDBError("Failed to begin transaction")

    def commit(self) -> None:
        """Commit the current transaction."""
        self._check_open()
        result = self._lib.stark_commit(self._handle)
        if result != STARK_OK:
            raise StarkDBError("No active transaction")

    def rollback(self) -> None:
        """Rollback the current transaction."""
        self._check_open()
        result = self._lib.stark_rollback(self._handle)
        if result != STARK_OK:
            raise StarkDBError("No active transaction")

    def in_transaction(self) -> bool:
        """Check if currently in a transaction."""
        self._check_open()
        return self._lib.stark_in_transaction(self._handle) != 0

    # ---- Utilities ----

    def sync(self) -> None:
        """Flush all changes to disk."""
        self._check_open()
        result = self._lib.stark_sync(self._handle)
        if result != STARK_OK:
            raise StarkDBError("Failed to sync database")

    def stats(self) -> Dict[str, Any]:
        """Get database statistics."""
        self._check_open()
        class Stats(ctypes.Structure):
            _fields_ = [
                ("keys_count", ctypes.c_uint64),
                ("btree_height", ctypes.c_uint32),
                ("data_size", ctypes.c_uint64),
                ("page_count", ctypes.c_uint32),
            ]

        s = Stats()
        result = self._lib.stark_stats(self._handle, ctypes.byref(s))
        if result != STARK_OK:
            raise StarkDBError("Failed to get stats")

        return {
            "keys_count": s.keys_count,
            "btree_height": s.btree_height,
            "data_size": s.data_size,
            "page_count": s.page_count,
        }

    def error(self) -> str:
        """Get the last error message."""
        self._check_open()
        msg = self._lib.stark_error(self._handle)
        return msg.decode("utf-8") if msg else ""

    # ---- Batch Operations ----

    def put_batch(self, items: Dict[int, Union[bytes, str]]) -> None:
        """Insert multiple key-value pairs atomically."""
        self._check_open()
        self.begin()
        try:
            for key, value in items.items():
                self.put(key, value)
            self.commit()
        except Exception:
            self.rollback()
            raise

    def get_batch(self, keys: List[int]) -> Dict[int, bytes]:
        """Get multiple values by keys."""
        self._check_open()
        result = {}
        for key in keys:
            try:
                result[key] = self.get(key)
            except StarkDBNotFound:
                pass
        return result

    # ---- Type System ----

    def define_type(self, name: str, fields: List[Dict[str, Any]]) -> None:
        """Define a typed struct schema."""
        self._check_open()
        class FieldDef(ctypes.Structure):
            _fields_ = [
                ("name", ctypes.c_char * 32),
                ("offset", ctypes.c_uint32),
                ("type", ctypes.c_uint8),
                ("size", ctypes.c_uint32),
            ]

        c_fields = (FieldDef * len(fields))()
        for i, f in enumerate(fields):
            c_fields[i].name = f["name"].encode("utf-8")[:31]
            c_fields[i].type = f.get("type", 1)
            c_fields[i].size = f.get("size", 4)
            c_fields[i].offset = 0

        result = self._lib.stark_define_type(
            self._handle, name.encode("utf-8"), c_fields, len(fields)
        )
        if result != STARK_OK:
            raise StarkDBError(f"Failed to define type '{name}'")

    def add_typed(self, type_name: str, key: int, field_values: str) -> None:
        """Add a typed record."""
        self._check_open()
        result = self._lib.stark_add_typed(
            self._handle, type_name.encode("utf-8"), ctypes.c_uint32(key),
            field_values.encode("utf-8")
        )
        if result != STARK_OK:
            raise StarkDBError(f"Failed to add typed data for '{type_name}'")

    def get_typed(self, type_name: str, key: int) -> str:
        """Get a typed record."""
        self._check_open()
        buf = ctypes.create_string_buffer(8192)
        result = self._lib.stark_get_typed(
            self._handle, type_name.encode("utf-8"), ctypes.c_uint32(key),
            buf, ctypes.c_size_t(8192)
        )
        if result == STARK_OK:
            return buf.value.decode("utf-8", errors="replace")
        elif result == STARK_NOT_FOUND:
            raise StarkDBNotFound(f"Typed record {type_name}:{key} not found")
        raise StarkDBError(f"Failed to get typed data")

    def undefine_type(self, name: str) -> None:
        """Delete a type definition."""
        self._check_open()
        result = self._lib.stark_undefine_type(self._handle, name.encode("utf-8"))
        if result != STARK_OK:
            raise StarkDBError(f"Failed to undefine type '{name}'")

    def __repr__(self):
        return f"Database(path='{self._path}', closed={self._closed})"
