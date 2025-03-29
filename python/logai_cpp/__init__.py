"""
LogAI Python Package

A hybrid package that imports functionality from both:
1. The C++ extension module (via pybind11)
2. Pure Python implementations
"""
import logging
import sys
import importlib.util
from typing import List, Dict, Any, Optional

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

# Try to import the C++ module first
try:
    # Get the directory of the current package
    import os.path
    package_dir = os.path.dirname(__file__)
    
    # Look for the compiled extension in the parent directory
    parent_dir = os.path.dirname(package_dir)
    extension_path = None
    
    # Look for files matching the pattern logai_cpp*.so
    for file in os.listdir(parent_dir):
        if file.startswith("logai_cpp") and file.endswith(".so"):
            extension_path = os.path.join(parent_dir, file)
            break
    
    if extension_path:
        spec = importlib.util.spec_from_file_location("_logai_cpp", extension_path)
        _logai_cpp = importlib.util.module_from_spec(spec)
        sys.modules["_logai_cpp"] = _logai_cpp
        spec.loader.exec_module(_logai_cpp)
        
        # Import the functions we want to keep from C++
        from _logai_cpp import (
            parse_log_file,
            process_large_file_with_callback,
            extract_attributes
        )
        
        # Log successful import
        logger.info("Successfully loaded LogAI C++ extension")
    else:
        logger.warning("LogAI C++ extension not found in parent directory")
        _logai_cpp = None
except ImportError as e:
    logger.warning(f"Failed to import LogAI C++ extension: {str(e)}")
    _logai_cpp = None

# Import Python implementations
from .embeddings import generate_template_embedding, GeminiVectorizer
from .milvus_client import (
    init_milvus,
    get_milvus_connection_string,
    insert_template,
    search_similar_templates
)

# Define what should be accessible when importing the package
__all__ = [
    # C++ functions
    "parse_log_file",
    "process_large_file_with_callback",
    "extract_attributes",
    
    # Python implementations for embeddings
    "generate_template_embedding",
    "GeminiVectorizer",
    
    # Python implementations for Milvus
    "init_milvus",
    "get_milvus_connection_string",
    "insert_template",
    "search_similar_templates"
] 