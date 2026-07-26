"""Low-level ctypes bindings to libstark."""
import ctypes
import ctypes.util
import os
import platform
import sys

_lib = None


def _find_library():
    """Find the libstark shared library."""
    search_names = ["libstark.so", "libstark.dylib", "stark.dll"]

    # Check build directory first
    script_dir = os.path.dirname(os.path.abspath(__file__))
    for pattern in [
        os.path.join(script_dir, "..", "..", "..", "build"),
        os.path.join(script_dir, "..", "..", "build"),
        os.getcwd(),
    ]:
        for name in search_names:
            path = os.path.join(pattern, name)
            if os.path.isfile(path):
                return path

    # Check system paths
    for name in search_names:
        try:
            path = ctypes.util.find_library(name.replace("lib", "").split(".")[0])
            if path:
                return path
        except Exception:
            pass

    # LD_LIBRARY_PATH
    ld_path = os.environ.get("LD_LIBRARY_PATH", "")
    for d in ld_path.split(":") if ld_path else []:
        for name in search_names:
            path = os.path.join(d, name)
            if os.path.isfile(path):
                return path

    raise RuntimeError(
        "Cannot find libstark shared library. Build it first:\n"
        "  cd build && cmake .. && make\n"
        "Or set STARK_LIB_PATH environment variable."
    )


def _init_lib():
    global _lib
    if _lib is not None:
        return _lib

    lib_path = os.environ.get("STARK_LIB_PATH", _find_library())
    _lib = ctypes.CDLL(lib_path)

    # stark_db_t* stark_open(const char* path, unsigned flags)
    _lib.stark_open.argtypes = [ctypes.c_char_p, ctypes.c_uint]
    _lib.stark_open.restype = ctypes.c_void_p

    # void stark_close(stark_db_t* db)
    _lib.stark_close.argtypes = [ctypes.c_void_p]
    _lib.stark_close.restype = None

    # stark_result_t stark_add(stark_db_t* db, uint32_t key, const void* value, size_t value_size)
    _lib.stark_add.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_size_t]
    _lib.stark_add.restype = ctypes.c_int

    # stark_result_t stark_put(...)
    _lib.stark_put.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_size_t]
    _lib.stark_put.restype = ctypes.c_int

    # stark_result_t stark_get(stark_db_t* db, uint32_t key, void* buffer, size_t* buffer_size)
    _lib.stark_get.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t)]
    _lib.stark_get.restype = ctypes.c_int

    # stark_result_t stark_delete(stark_db_t* db, uint32_t key)
    _lib.stark_delete.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    _lib.stark_delete.restype = ctypes.c_int

    # int stark_exists(stark_db_t* db, uint32_t key)
    _lib.stark_exists.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    _lib.stark_exists.restype = ctypes.c_int

    # stark_result_t stark_put_str(...)
    _lib.stark_put_str.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_size_t]
    _lib.stark_put_str.restype = ctypes.c_int

    # stark_result_t stark_get_str(...)
    _lib.stark_get_str.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t)]
    _lib.stark_get_str.restype = ctypes.c_int

    # stark_result_t stark_del_str(...)
    _lib.stark_del_str.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.stark_del_str.restype = ctypes.c_int

    # int stark_exists_str(...)
    _lib.stark_exists_str.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.stark_exists_str.restype = ctypes.c_int

    # stark_result_t stark_sync(...)
    _lib.stark_sync.argtypes = [ctypes.c_void_p]
    _lib.stark_sync.restype = ctypes.c_int

    # stark_result_t stark_stats(...)
    _lib.stark_stats.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    _lib.stark_stats.restype = ctypes.c_int

    # const char* stark_error(...)
    _lib.stark_error.argtypes = [ctypes.c_void_p]
    _lib.stark_error.restype = ctypes.c_char_p

    # Cursor
    _lib.stark_cursor_create.argtypes = [ctypes.c_void_p]
    _lib.stark_cursor_create.restype = ctypes.c_void_p

    _lib.stark_cursor_first.argtypes = [ctypes.c_void_p]
    _lib.stark_cursor_first.restype = ctypes.c_int

    _lib.stark_cursor_next.argtypes = [ctypes.c_void_p]
    _lib.stark_cursor_next.restype = ctypes.c_int

    _lib.stark_cursor_prev.argtypes = [ctypes.c_void_p]
    _lib.stark_cursor_prev.restype = ctypes.c_int

    _lib.stark_cursor_last.argtypes = [ctypes.c_void_p]
    _lib.stark_cursor_last.restype = ctypes.c_int

    _lib.stark_cursor_get.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32),
                                       ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t)]
    _lib.stark_cursor_get.restype = ctypes.c_int

    _lib.stark_cursor_destroy.argtypes = [ctypes.c_void_p]
    _lib.stark_cursor_destroy.restype = None

    # Binary
    _lib.stark_store_binary.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_int,
                                         ctypes.c_void_p, ctypes.c_size_t, ctypes.c_size_t]
    _lib.stark_store_binary.restype = ctypes.c_int

    _lib.stark_load_binary.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.POINTER(ctypes.c_int),
                                        ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t),
                                        ctypes.POINTER(ctypes.c_size_t)]
    _lib.stark_load_binary.restype = ctypes.c_int

    _lib.stark_get_binary_ptr.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.POINTER(ctypes.c_int),
                                           ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_size_t),
                                           ctypes.POINTER(ctypes.c_size_t)]
    _lib.stark_get_binary_ptr.restype = ctypes.c_int

    # Columns
    _lib.stark_store_columns.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p,
                                          ctypes.c_void_p, ctypes.c_void_p,
                                          ctypes.c_size_t, ctypes.c_size_t]
    _lib.stark_store_columns.restype = ctypes.c_int

    _lib.stark_load_columns.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p]
    _lib.stark_load_columns.restype = ctypes.c_int

    _lib.stark_free_columns.argtypes = [ctypes.c_void_p]
    _lib.stark_free_columns.restype = None

    # Transactions
    _lib.stark_begin.argtypes = [ctypes.c_void_p]
    _lib.stark_begin.restype = ctypes.c_int

    _lib.stark_commit.argtypes = [ctypes.c_void_p]
    _lib.stark_commit.restype = ctypes.c_int

    _lib.stark_rollback.argtypes = [ctypes.c_void_p]
    _lib.stark_rollback.restype = ctypes.c_int

    _lib.stark_in_transaction.argtypes = [ctypes.c_void_p]
    _lib.stark_in_transaction.restype = ctypes.c_int

    # Type system
    _lib.stark_define_type.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_uint32]
    _lib.stark_define_type.restype = ctypes.c_int

    _lib.stark_get_type.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.stark_get_type.restype = ctypes.c_void_p

    _lib.stark_undefine_type.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.stark_undefine_type.restype = ctypes.c_int

    _lib.stark_add_typed.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint32, ctypes.c_char_p]
    _lib.stark_add_typed.restype = ctypes.c_int

    _lib.stark_get_typed.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint32,
                                      ctypes.c_void_p, ctypes.c_size_t]
    _lib.stark_get_typed.restype = ctypes.c_int

    return _lib


def get_lib():
    """Get or initialize the libstark handle."""
    global _lib
    if _lib is None:
        _init_lib()
    return _lib


# Result codes
STARK_OK = 0
STARK_ERROR = -1
STARK_NOT_FOUND = -2
STARK_FULL = -3
STARK_IO_ERROR = -4
STARK_INVALID_ARG = -5
STARK_CLOSED = -6
STARK_MEMORY_ERROR = -7


def _check_result(result):
    """Raise appropriate exception based on result code."""
    if result < 0:
        codes = {
            -2: StarkDBNotFound,
            -3: StarkDBFull,
            -4: StarkDBIOError,
            -5: StarkDBValueError,
            -6: StarkDBClosed,
            -7: StarkDBMemoryError,
        }
        exc_class = codes.get(result, StarkDBError)
        raise exc_class(f"STARK error code: {result}")
    return result
