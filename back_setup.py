from distutils.core import setup, Extension 
from setuptools import find_packages
import glob
import os
import sys

# the c++ extension module
sources = glob.glob("dx7core/*.cc")
sources.remove("dx7core/main.cc")
sources.remove("dx7core/test_ringbuffer.cc")
sources.remove("dx7core/test_filter.cc")
sources.remove("dx7core/test_neon.cc")
os.environ["CC"] = "gcc"
os.environ["CXX"] = "g++"

extension_mod = Extension(
    name="dx7",
    sources=sources,
    extra_compile_args = None #["-O0"], 
    )

packs = find_packages(where="bin")
print(packs)
setup(
    name = "dx7",
    ext_modules=[extension_mod],
    version="1.0.0",
    zip_safe=False,
    packages=packs,
    package_dir={"": "bin"},
    package_data={"": ["compact.bin"]}
)

