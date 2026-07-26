"""Setup script for starkdb Python package."""
from setuptools import setup, find_packages

with open("starkdb/__init__.py") as f:
    for line in f:
        if line.startswith("__version__"):
            version = line.split("=")[1].strip().strip('"')
            break

setup(
    name="starkdb",
    version=version,
    packages=find_packages(),
    package_data={"starkdb": ["py.typed"]},
    description="Python bindings for STARKDB - embedded key-value database with ML support",
    long_description=open("README.md").read() if __import__("os").path.exists("README.md") else "",
    long_description_content_type="text/markdown",
    author="Abdullah Novruzlu",
    license="MIT",
    url="https://starkdb.org",
    python_requires=">=3.8",
    classifiers=[
        "Programming Language :: Python :: 3",
        "License :: OSI Approved :: MIT License",
        "Topic :: Database",
        "Topic :: Scientific/Engineering :: Artificial Intelligence",
    ],
)
