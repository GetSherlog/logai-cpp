"""
Milvus client for LogAI
"""
import logging
import requests
from typing import List, Dict, Any

try:
    from pymilvus import (
        connections, 
        utility,
        FieldSchema, 
        CollectionSchema,
        DataType,
        Collection
    )
    HAVE_MILVUS = True
except ImportError:
    HAVE_MILVUS = False
    
logger = logging.getLogger(__name__)

# Global variables to store connection state
_milvus_host = "milvus"
_milvus_port = 19530
_is_connected = False
_collection_name = "log_templates"
_collection = None
_dim = 768  # Default embedding dimension

def init_milvus(host: str = "milvus", port: int = 19530) -> bool:
    """
    Initialize connection to Milvus server
    
    Args:
        host: Milvus server hostname or IP
        port: Milvus server port
        
    Returns:
        True if connection successful, False otherwise
    """
    global _milvus_host, _milvus_port, _is_connected
    
    if not HAVE_MILVUS:
        logger.error("PyMilvus is not installed. Please install with 'pip install pymilvus'")
        return False
    
    _milvus_host = host
    _milvus_port = port
    
    try:
        # Try a direct HTTP health check first
        url = f"http://{host}:{port}/health"
        response = requests.get(url, timeout=5)
        
        if not response.ok:
            logger.error(f"Health check failed: {response.status_code}")
            return False
        
        # Pymilvus connection
        connections.connect(
            alias="default",
            host=host,
            port=port
        )
        
        _is_connected = True
        logger.info(f"Successfully connected to Milvus server at {host}:{port}")
        
        # Initialize collection
        _init_collection()
        
        return True
    except Exception as e:
        logger.error(f"Failed to connect to Milvus server: {str(e)}")
        _is_connected = False
        return False

def _init_collection(collection_name: str = "log_templates", dim: int = 768) -> bool:
    """
    Initialize the collection in Milvus
    
    Args:
        collection_name: Name of the collection
        dim: Dimension of the embedding vectors
        
    Returns:
        True if successful, False otherwise
    """
    global _collection, _collection_name, _dim
    
    if not HAVE_MILVUS or not _is_connected:
        return False
    
    _collection_name = collection_name
    _dim = dim
    
    try:
        # Check if collection exists
        if utility.has_collection(collection_name):
            _collection = Collection(collection_name)
            _collection.load()
            logger.info(f"Loaded existing collection '{collection_name}'")
            return True
            
        # Define fields for the collection
        fields = [
            FieldSchema(name="id", dtype=DataType.VARCHAR, is_primary=True, max_length=100),
            FieldSchema(name="template", dtype=DataType.VARCHAR, max_length=2000),
            FieldSchema(name="count", dtype=DataType.INT64),
            FieldSchema(name="embedding", dtype=DataType.FLOAT_VECTOR, dim=dim)
        ]
        
        # Create collection schema
        schema = CollectionSchema(fields=fields, description="Log template collection")
        
        # Create collection
        _collection = Collection(name=collection_name, schema=schema)
        
        # Create index on vector field
        index_params = {
            "metric_type": "COSINE",
            "index_type": "IVF_FLAT",
            "params": {"nlist": 128}
        }
        _collection.create_index(field_name="embedding", index_params=index_params)
        
        # Load collection
        _collection.load()
        
        logger.info(f"Created new collection '{collection_name}'")
        return True
    except Exception as e:
        logger.error(f"Failed to initialize collection: {str(e)}")
        return False

def get_milvus_connection_string() -> str:
    """
    Get the Milvus connection string
    
    Returns:
        Connection string in format 'host:port'
    """
    global _milvus_host, _milvus_port
    return f"{_milvus_host}:{_milvus_port}"

def insert_template(template_id: str, template: str, count: int, embedding: List[float]) -> bool:
    """
    Insert a template with its embedding into Milvus
    
    Args:
        template_id: Unique identifier for the template
        template: The template string
        count: Number of occurrences
        embedding: Vector embedding of the template
        
    Returns:
        True if successful, False otherwise
    """
    global _collection
    
    if not HAVE_MILVUS or not _is_connected or _collection is None:
        logger.error("Milvus not initialized")
        return False
    
    try:
        # Check if the template already exists
        search_params = {"expr": f'id == "{template_id}"'}
        results = _collection.query(search_params)
        
        if results:
            # Update existing template
            _collection.delete(f'id == "{template_id}"')
        
        # Insert the template
        _collection.insert([
            [template_id],        # id
            [template],           # template
            [count],              # count
            [embedding]           # embedding
        ])
        
        return True
    except Exception as e:
        logger.error(f"Failed to insert template: {str(e)}")
        return False

def search_similar_templates(query_embedding: List[float], top_k: int = 5) -> List[Dict[str, Any]]:
    """
    Search for templates similar to the query embedding
    
    Args:
        query_embedding: Vector embedding to search for
        top_k: Number of results to return
        
    Returns:
        List of templates with similarity scores
    """
    global _collection
    
    if not HAVE_MILVUS or not _is_connected or _collection is None:
        logger.error("Milvus not initialized")
        return []
    
    try:
        # Prepare search parameters
        search_params = {
            "metric_type": "COSINE",
            "params": {"nprobe": 10}
        }
        
        # Perform search
        results = _collection.search(
            data=[query_embedding],
            anns_field="embedding",
            param=search_params,
            limit=top_k,
            output_fields=["template", "count"]
        )
        
        # Process results
        templates = []
        for hits in results:
            for hit in hits:
                templates.append({
                    "template_id": hit.id,
                    "template": hit.entity.get("template"),
                    "count": hit.entity.get("count"),
                    "score": hit.score
                })
        
        return templates
    except Exception as e:
        logger.error(f"Failed to search templates: {str(e)}")
        return [] 