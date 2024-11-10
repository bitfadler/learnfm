from distutils.core import setup, Extension
import glob
import os
# the c++ extension module
sources = glob.glob("dx7core/*.cc")
sources.remove("dx7core/main.cc")
sources.remove("dx7core/test_ringbuffer.cc")
sources.remove("dx7core/test_filter.cc")
sources.remove("dx7core/test_neon.cc")
os.environ["CC"] = "gcc"
os.environ["CXX"] = "g++"

extension_mod = Extension("dx7", sources=sources)

setup(name = "dx7", ext_modules=[extension_mod])
