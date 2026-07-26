"""Setup script for starkdb Python package."""
import os
from setuptools import setup, find_packages

here = os.path.abspath(os.path.dirname(__file__))

version = "1.1.0"
version_file = os.path.join(here, "starkdb", "__init__.py")
if os.path.exists(version_file):
    with open(version_file) as f:
        for line in f:
            if line.startswith("__version__"):
                version = line.split("=")[1].strip().strip('"')
                break

readme_path = os.path.join(here, "README.md")
long_description = ""
if os.path.exists(readme_path):
    with open(readme_path) as f:
        long_description = f.read()

setup(
    name="starkdb",
    version=version,
    packages=find_packages(),
    include_package_data=True,
    description="Python bindings for STARKDB - embedded key-value database with ML support",
    long_description=long_description,
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
        "Intended Audience :: Developers",
        "Intended Audience :: Science/Research",
    ],
)
