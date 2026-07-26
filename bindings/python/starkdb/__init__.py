"""
starkdb - Python bindings for STARK Database

STARK is a lightweight embedded key-value database with B-tree indexing.
This package provides Pythonic access with NumPy zero-copy array support
and Arrow column layout for DataFrame integration.

Basic usage:
    import starkdb
    db = starkdb.Database("mydb")
    db["player"] = "Hero"
    print(db["player"])
    db.close()

Array usage:
    import numpy as np
    arr = np.array([1.0, 2.0, 3.0], dtype=np.float32)
    db.store_array("embeddings", arr)
    loaded = db.load_array("embeddings")  # Zero-copy numpy array

DataFrame usage:
    import pandas as pd
    df = pd.DataFrame({"name": ["Alice", "Bob"], "age": [25, 30]})
    db.store_dataframe("users", df)
    df2 = db.load_dataframe("users")
"""

from .database import Database, StarkDBError, StarkDBNotFound

__version__ = "1.1.0"
__all__ = ["Database", "StarkDBError", "StarkDBNotFound"]
