#!/usr/bin/env python3
import os
import sys
import platform
from setuptools import setup, Extension, find_packages
from setuptools.command.build_ext import build_ext

# Get the absolute path to the project root
PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SRC_DIR = os.path.join(PROJECT_ROOT, "src")

class CMakeExtension(Extension):
    def __init__(self, name, cmake_lists_dir='.', **kwa):
        Extension.__init__(self, name, sources=[], **kwa)
        self.cmake_lists_dir = os.path.abspath(cmake_lists_dir)

class CMakeBuild(build_ext):
    def build_extension(self, ext):
        ext_dir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))
        
        # Required for auto-detection of auxiliary "native" libs
        if not ext_dir.endswith(os.path.sep):
            ext_dir += os.path.sep

        cmake_args = [
            f'-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={ext_dir}',
            f'-DPYTHON_EXECUTABLE={sys.executable}',
            f'-DCMAKE_BUILD_TYPE=Release',
        ]

        build_args = [
            '--config', 'Release',
            '--parallel', '4'
        ]

        os.makedirs(self.build_temp, exist_ok=True)
        
        # Build the entire C++ project first
        self.spawn(['cmake', ext.cmake_lists_dir] + cmake_args, cwd=self.build_temp)
        self.spawn(['cmake', '--build', '.'] + build_args, cwd=self.build_temp)

setup(
    name="logai-cpp",
    version="0.1.0",
    author="LogAI Team",
    author_email="info@logai.com",
    description="Python bindings for LogAI C++ library",
    long_description="",
    ext_modules=[CMakeExtension("logai_cpp", PROJECT_ROOT)],
    cmdclass={"build_ext": CMakeBuild},
    packages=find_packages(),
    install_requires=[
        "instructor>=0.5.0",
        "openai>=1.0.0",
        "pydantic>=2.0.0",
        "rich>=13.0.0",
        "pybind11>=2.10.0",
        "duckdb>=0.10.0",
        "pymilvus>=2.3.0",
        "python-dotenv>=1.0.0",
        "tqdm>=4.66.0",
        "numpy>=1.24.0",
        "pandas>=2.0.0"
    ],
    classifiers=[
        "Programming Language :: Python :: 3",
        "License :: OSI Approved :: MIT License",
        "Operating System :: OS Independent",
    ],
    python_requires=">=3.8",
    entry_points={
        "console_scripts": [
            "logai-agent=logai_agent:main",
        ],
    },
) 