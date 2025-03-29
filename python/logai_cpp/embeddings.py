"""
Embedding generation using Gemini API for LogAI
"""
import os
import json
import requests
from typing import Optional, List, Dict, Any
import logging

logger = logging.getLogger(__name__)

class GeminiVectorizer:
    """Gemini API vectorizer for generating embeddings"""
    
    def __init__(self, 
                 model_name: str = "gemini-embedding-exp-03-07",
                 api_key: Optional[str] = None,
                 use_env_api_key: bool = True,
                 api_key_env_var: str = "GEMINI_API_KEY",
                 embedding_dim: int = 768,
                 cache_capacity: int = 1000):
        """
        Initialize the Gemini vectorizer
        
        Args:
            model_name: Name of the Gemini model to use
            api_key: API key for Gemini API (if not using environment variable)
            use_env_api_key: Whether to use API key from environment variable
            api_key_env_var: Environment variable name for API key
            embedding_dim: Dimension of the embeddings
            cache_capacity: Maximum number of entries in the embedding cache
        """
        self.model_name = model_name
        self.api_key = api_key
        self.use_env_api_key = use_env_api_key
        self.api_key_env_var = api_key_env_var
        self.embedding_dim = embedding_dim
        self.cache_capacity = cache_capacity
        self.embedding_cache = {}
    
    def get_api_key(self) -> str:
        """Get the API key from environment or instance variable"""
        if not self.use_env_api_key:
            return self.api_key or ""
        
        return os.environ.get(self.api_key_env_var, "")
    
    def build_request_url(self) -> str:
        """Build the URL for the Gemini API request"""
        base_url = "https://generativelanguage.googleapis.com"
        return f"{base_url}/v1/models/{self.model_name}:embedContent"
    
    def build_request_payload(self, text: str) -> Dict[str, Any]:
        """Build the payload for the Gemini API request"""
        content = {"parts": [{"text": text}]}
        return {
            "model": self.model_name,
            "contents": [content]
        }
    
    def get_embedding(self, text: str) -> Optional[List[float]]:
        """
        Get embedding for text using Gemini API
        
        Args:
            text: Text to generate embedding for
            
        Returns:
            List of embedding values or None if failed
        """
        # Check cache first
        if text in self.embedding_cache:
            return self.embedding_cache[text]
        
        api_key = self.get_api_key()
        if not api_key:
            logger.error("Gemini API key not found")
            return None
        
        url = self.build_request_url()
        payload = self.build_request_payload(text)
        
        # Add API key as a query parameter
        params = {"key": api_key}
        
        try:
            response = requests.post(
                url, 
                params=params,
                json=payload,
                headers={"Content-Type": "application/json"},
                timeout=30
            )
            
            if not response.ok:
                logger.error(f"API request failed: {response.status_code} - {response.text}")
                return None
            
            data = response.json()
            
            # Extract embedding from response
            embedding = None
            if "embedding" in data:
                if "values" in data["embedding"]:
                    embedding = data["embedding"]["values"]
                else:
                    embedding = data["embedding"]
            elif "embeddings" in data and isinstance(data["embeddings"], list) and len(data["embeddings"]) > 0:
                embedding = data["embeddings"][0]
            
            if embedding:
                # Manage cache size
                if len(self.embedding_cache) >= self.cache_capacity:
                    # Simple strategy: remove a random entry
                    if self.embedding_cache:
                        self.embedding_cache.pop(next(iter(self.embedding_cache)))
                
                # Cache the result
                self.embedding_cache[text] = embedding
                
            return embedding
            
        except Exception as e:
            logger.error(f"Error generating embedding: {str(e)}")
            return None
    
    def is_valid(self) -> bool:
        """Check if API key is valid and API is accessible"""
        api_key = self.get_api_key()
        if not api_key:
            return False
        
        # Try to get an embedding for a test message
        test_embedding = self.get_embedding("Test message")
        return test_embedding is not None

# Function to generate embedding compatible with C++ function
def generate_template_embedding(template_text: str) -> List[float]:
    """
    Generate embedding for a template using Gemini API
    
    Args:
        template_text: Template text to generate embedding for
        
    Returns:
        List of embedding values or empty list if failed
    """
    try:
        # Global vectorizer instance to maintain state
        global _vectorizer
        if '_vectorizer' not in globals():
            _vectorizer = GeminiVectorizer()
        
        # Generate embedding
        embedding = _vectorizer.get_embedding(template_text)
        if embedding is None:
            logger.error("Failed to generate embedding using Gemini")
            return []
        
        return embedding
    except Exception as e:
        logger.error(f"Error generating embedding: {str(e)}")
        return [] 