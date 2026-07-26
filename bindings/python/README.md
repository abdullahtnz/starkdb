# STARKDB Python Bindings

Python bindings for STARKDB - a lightweight embedded key-value database with ML features.

## Features

- Full CRUD operations (numeric and string keys)
- Dict-like syntax: `db[key] = value`
- Cursor-based iteration
- NumPy zero-copy binary array storage
- Arrow column layout for Pandas DataFrames
- Transactions, type system, batch operations

## Quick Start

```python
import starkdb

db = starkdb.Database("mydb")
db[1] = b"Hello World"
print(db[1])
db.close()
```

## ML Features

```python
import numpy as np
import starkdb

db = starkdb.Database("ml_data")

# Store embeddings as contiguous binary
arr = np.array([0.1, 0.5, -0.3], dtype=np.float32)
db.store_array(1, arr)

# Load with zero-copy (direct memory pointer)
loaded = db.load_array_zerocopy(1)
```

## Install

```bash
# Build C library first
cd starkdb && mkdir build && cd build
cmake .. && make

# Install Python bindings
cd ../bindings/python
pip install -e .
```

Set `STARK_LIB_PATH` if libstark.so is not in a standard path.
