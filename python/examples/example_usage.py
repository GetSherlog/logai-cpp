#!/usr/bin/env python3
"""
Example usage of the LogAI Agent with different providers.
This script demonstrates how to initialize and use the pydantic-ai based LogAI agent
with OpenAI, Google Gemini, Anthropic Claude, and Ollama.
"""

import os
from logai_agent import LogAIAgent
from dotenv import load_dotenv

# Load environment variables from .env file if present
load_dotenv()

def example_openai():
    """Example using OpenAI provider."""
    print("\n=== Using OpenAI Provider ===")
    
    # Initialize with API key from environment variable (OPENAI_API_KEY)
    agent = LogAIAgent(provider="openai")
    
    # Or explicitly provide API key and model
    # agent = LogAIAgent(
    #     provider="openai",
    #     api_key="your-openai-api-key",
    #     model="gpt-4o"
    # )
    
    # Initialize with a log file
    agent.initialize("path/to/your/logfile.log")
    
    # Process a query
    response = agent.process_query("What are the most common error patterns in these logs?")
    print(f"Response: {response}")

def example_gemini():
    """Example using Google Gemini provider."""
    print("\n=== Using Google Gemini Provider ===")
    
    try:
        # Initialize with API key from environment variable (GEMINI_API_KEY)
        agent = LogAIAgent(provider="gemini")
        
        # Or explicitly provide API key and model
        # agent = LogAIAgent(
        #     provider="gemini",
        #     api_key="your-gemini-api-key",
        #     model="gemini-1.5-flash"
        # )
        
        # Initialize with a log file
        agent.initialize("path/to/your/logfile.log")
        
        # Process a query
        response = agent.process_query("How many ERROR level logs are there?")
        print(f"Response: {response}")
    except ImportError as e:
        print(f"Skipping Gemini example: {e}")

def example_claude():
    """Example using Anthropic Claude provider."""
    print("\n=== Using Anthropic Claude Provider ===")
    
    try:
        # Initialize with API key from environment variable (ANTHROPIC_API_KEY)
        agent = LogAIAgent(provider="claude")
        
        # Or explicitly provide API key and model
        # agent = LogAIAgent(
        #     provider="claude",
        #     api_key="your-claude-api-key",
        #     model="claude-3-opus-20240229"
        # )
        
        # Initialize with a log file
        agent.initialize("path/to/your/logfile.log")
        
        # Process a query
        response = agent.process_query("Summarize the system startup sequence.")
        print(f"Response: {response}")
    except ImportError as e:
        print(f"Skipping Claude example: {e}")

def example_ollama():
    """Example using Ollama provider."""
    print("\n=== Using Ollama Provider ===")
    
    try:
        # Initialize with default Ollama host
        agent = LogAIAgent(provider="ollama")
        
        # Or specify custom host and model
        # agent = LogAIAgent(
        #     provider="ollama",
        #     host="http://localhost:11434",
        #     model="llama3"
        # )
        
        # Initialize with a log file
        agent.initialize("path/to/your/logfile.log")
        
        # Process a query
        response = agent.process_query("Show me logs with high CPU usage.")
        print(f"Response: {response}")
    except ImportError as e:
        print(f"Skipping Ollama example: {e}")

if __name__ == "__main__":
    # Run examples
    example_openai()
    example_gemini()
    example_claude()
    example_ollama() 