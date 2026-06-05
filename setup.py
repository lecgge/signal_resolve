#!/usr/bin/env python3
"""USDE Python package — pip-installable wrapper.

Requires pybind11 and a C++20 compiler.

Install:
    pip install .
    # or
    python setup.py develop

Usage:
    import usde_python
    net = usde_python.Network()
    net.load_dbc("test.dbc")
    signals = net.decode_frame(0x100, raw_bytes)
"""

from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
import sys
import os

class get_pybind_include:
    """Helper to locate pybind11 headers."""
    def __init__(self, user=False):
        self.user = user
    def __str__(self):
        import pybind11
        return pybind11.get_include(self.user)

ext_modules = [
    Extension(
        "usde_python",
        sources=[
            "src/usde_pybind.cpp",
            "src/dbc_parser.cpp",
            "src/ldf_parser.cpp",
            "src/arxml_parser.cpp",
            "src/codec_engine.cpp",
        ],
        include_dirs=[
            "include",
            get_pybind_include(),
            get_pybind_include(user=True),
        ],
        language="c++",
        extra_compile_args=["/std:c++20", "/utf-8"] if sys.platform == "win32"
                           else ["-std=c++20"],
    ),
]

class BuildExt(build_ext):
    def build_extensions(self):
        import pybind11
        # Ensure pybind11 headers are found
        for ext in self.extensions:
            ext.include_dirs = [d for d in ext.include_dirs if os.path.isdir(str(d))]
        super().build_extensions()

setup(
    name="usde-python",
    version="1.0.0",
    author="USDE Team",
    description="Universal Signal Decoding Engine — Python binding",
    long_description=open("README.md", encoding="utf-8").read(),
    long_description_content_type="text/markdown",
    ext_modules=ext_modules,
    cmdclass={"build_ext": BuildExt},
    python_requires=">=3.8",
    install_requires=["pybind11>=2.10"],
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: MIT License",
        "Programming Language :: C++",
        "Programming Language :: Python :: 3",
    ],
)
