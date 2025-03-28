#!/usr/bin/env python3
import os
import sys
import json
import time
import argparse
from typing import List, Dict, Any, Optional, Union, Callable, Literal, Type, TypeVar
from dataclasses import dataclass
from pydantic import BaseModel, Field

# Import pydantic-ai
from pydantic_ai import Agent, RunContext
from pydantic_ai.models.openai import OpenAIModel
from pydantic_ai.models.anthropic import AnthropicModel
from pydantic_ai.models.gemini import GeminiModel
from pydantic_ai.models.openai import OpenAIModel
from pydantic_ai.providers.openai import OpenAIProvider

# Optional imports for Ollama
try:
    from pydantic_ai.providers.openai import OpenAIProvider
    HAS_OLLAMA = True
except ImportError:
    HAS_OLLAMA = False

# Helper for CLI styling
from rich.console import Console
from rich.markdown import Markdown
from rich.panel import Panel
from rich.prompt import Prompt
from rich.syntax import Syntax

# Data handling
import pandas as pd
import duckdb

# Import the C++ module directly
try:
    import logai_cpp
    HAS_CPP_MODULE = True
except ImportError:
    print("Warning: LogAI C++ module not found. Some functionality will be limited.")
    HAS_CPP_MODULE = False

# Define models for our agent tools
class LogTemplate(BaseModel):
    """A log template with its associated information."""
    template_id: int = Field(..., description="Unique identifier for the template")
    template: str = Field(..., description="The log template text")
    count: int = Field(..., description="Number of logs matching this template")
    
class LogAttributes(BaseModel):
    """Attributes extracted from logs for a given template."""
    template_id: int = Field(..., description="The template ID these attributes belong to")
    attributes: Dict[str, List[Any]] = Field(..., description="Dictionary of attribute name to list of values")

class SearchResult(BaseModel):
    """Result from searching logs."""
    logs: List[Dict[str, Any]] = Field(..., description="List of matching log entries")
    count: int = Field(..., description="Total count of matching logs")

class QueryResult(BaseModel):
    """Result from querying the database."""
    columns: List[str] = Field(..., description="Column names in the result")
    rows: List[List[Any]] = Field(..., description="Result rows from the query")
    
class TimeRange(BaseModel):
    """Time range information from logs."""
    start_time: str = Field(..., description="Start time of logs in ISO format")
    end_time: str = Field(..., description="End time of logs in ISO format")
    duration_seconds: float = Field(..., description="Duration in seconds")

class CountResult(BaseModel):
    """Result of counting occurrences."""
    total: int = Field(..., description="Total count of matched items")
    breakdown: Dict[str, int] = Field(default_factory=dict, description="Count breakdown by categories")

# Define agent dependencies and result models
@dataclass
class LogAIDependencies:
    """Dependencies for the LogAI agent."""
    log_file: Optional[str] = None
    is_initialized: bool = False
    cpp_wrapper: Any = None
    console: Any = None

class LogAnalysisStep(BaseModel):
    """A step in the log analysis process."""
    thought: str = Field(..., description="The agent's thinking process for this step")
    tool: str = Field(..., description="The tool to use for this step")
    tool_input: Dict[str, Any] = Field(..., description="The input parameters for the tool")

class LogAnalysisResult(BaseModel):
    """The final result of log analysis."""
    steps: List[LogAnalysisStep] = Field(..., description="Steps taken to analyze the logs")
    final_answer: str = Field(..., description="The final answer to the user's question")

# Default model mapping for different providers
DEFAULT_MODELS = {
    "openai": "gpt-4o",
    "gemini": "gemini-1.5-flash",
    "claude": "claude-3-sonnet-20240229",
    "ollama": "llama3"
}

# Provider Type
ProviderType = Literal["openai", "gemini", "claude", "ollama"]

# Main LogAI Agent class
class LogAIAgent:
    """LogAI Agent for analyzing logs with AI assistance."""
    
    def __init__(self, provider: Literal["openai", "gemini", "claude", "ollama"] = "openai", 
                 api_key: Optional[str] = None, 
                 model: Optional[str] = None,
                 host: Optional[str] = "http://localhost:11434"):
        """Initialize the LogAI Agent."""
        self.console = Console()
        self.provider = provider
        self.is_initialized = False
        self.log_file = None
        self.cpp_wrapper = logai_cpp
        self.api_key = api_key
        self.model = None
        self.host = host
        
        # Set up model based on provider
        self._setup_model(provider, model, api_key, host)
        
    def _setup_model(self, provider: str, model: Optional[str] = None, api_key: Optional[str] = None, host: Optional[str] = None):
        """Set up the appropriate AI model based on provider."""
        model_name = model or DEFAULT_MODELS.get(provider)
        
        if provider == "openai":
            from pydantic_ai.models.openai import OpenAIModel
            self.model = OpenAIModel(api_key=api_key, model=model_name)
        elif provider == "gemini":
            from pydantic_ai.models.gemini import GeminiModel
            self.model = GeminiModel(api_key=api_key, model=model_name)
        elif provider == "claude":
            from pydantic_ai.models.anthropic import AnthropicModel
            self.model = AnthropicModel(api_key=api_key, model=model_name)
        elif provider == "ollama" and HAS_OLLAMA:
            from pydantic_ai.providers.openai import OpenAIProvider
            # Configure Ollama with OpenAI-compatible provider
            self.model = OpenAIProvider(base_url=host, api_key="", model=model_name)
        else:
            raise ValueError(f"Unsupported provider: {provider}")

    def initialize(self, log_file: str, format: Optional[str] = None) -> bool:
        """Initialize the agent with a log file."""
        self.console.print(f"[bold blue]Initializing LogAI Agent with file:[/] {log_file}")
        
        # Parse the log file using the C++ wrapper
        if not self.cpp_wrapper.parse_log_file(log_file, format or ""):
            self.console.print("[bold red]Failed to parse log file.[/]")
            return False
        
        # Extract templates
        if not self.cpp_wrapper.extract_templates():
            self.console.print("[bold red]Failed to extract templates.[/]")
            return False
        
        # Store templates in Milvus
        if not self.cpp_wrapper.store_templates_in_milvus():
            self.console.print("[bold red]Failed to store templates in Milvus.[/]")
            return False
        
        # Store attributes in DuckDB
        if not self.cpp_wrapper.store_attributes_in_duckdb():
            self.console.print("[bold red]Failed to store attributes in DuckDB.[/]")
            return False
        
        self.log_file = log_file
        self.is_initialized = True
        
        self.console.print("[bold green]✓[/] Log file parsed successfully")
        self.console.print("[bold green]✓[/] Templates extracted and stored")
        self.console.print("[bold green]✓[/] Attributes stored in DuckDB")
        
        return True

    def search_logs(self, query: str, limit: int = 10) -> List[Dict[str, Any]]:
        """Search logs matching a pattern."""
        if not self.is_initialized:
            self.console.print("[bold red]Error: Agent not initialized. Call initialize() first.[/]")
            return []
            
        try:
            results = self.cpp_wrapper.search_logs(query, limit)
            return json.loads(results)["logs"]
        except Exception as e:
            self.console.print(f"[bold red]Error searching logs:[/] {str(e)}")
            return []

    def execute_query(self, query: str) -> Dict[str, Any]:
        """Execute a SQL query against the log data."""
        if not self.is_initialized:
            self.console.print("[bold red]Error: Agent not initialized. Call initialize() first.[/]")
            return {"columns": [], "rows": []}
            
        try:
            results = self.cpp_wrapper.execute_query(query)
            return json.loads(results)
        except Exception as e:
            self.console.print(f"[bold red]Error executing query:[/] {str(e)}")
            return {"columns": [], "rows": []}

    def get_template(self, template_id: int) -> Optional[Dict[str, Any]]:
        """Get information about a specific template."""
        if not self.is_initialized:
            self.console.print("[bold red]Error: Agent not initialized. Call initialize() first.[/]")
            return None
            
        try:
            results = self.cpp_wrapper.get_template(template_id)
            return json.loads(results)
        except Exception as e:
            self.console.print(f"[bold red]Error getting template:[/] {str(e)}")
            return None

    def get_time_range(self) -> Dict[str, str]:
        """Get the time range of the loaded logs."""
        if not self.is_initialized:
            self.console.print("[bold red]Error: Agent not initialized. Call initialize() first.[/]")
            return {"start": "", "end": ""}
            
        try:
            results = self.cpp_wrapper.get_time_range()
            return json.loads(results)
        except Exception as e:
            self.console.print(f"[bold red]Error getting time range:[/] {str(e)}")
            return {"start": "", "end": ""}

    def count_occurrences(self, pattern: str, group_by: Optional[str] = None) -> Dict[str, Any]:
        """Count occurrences of a pattern in the logs."""
        if not self.is_initialized:
            self.console.print("[bold red]Error: Agent not initialized. Call initialize() first.[/]")
            return {"count": 0, "groups": {}}
            
        try:
            results = self.cpp_wrapper.count_occurrences(pattern, group_by or "")
            return json.loads(results)
        except Exception as e:
            self.console.print(f"[bold red]Error counting occurrences:[/] {str(e)}")
            return {"count": 0, "groups": {}}

    def summarize_logs(self, time_range: Optional[Dict[str, str]] = None) -> Dict[str, Any]:
        """Generate a summary of the logs."""
        if not self.is_initialized:
            self.console.print("[bold red]Error: Agent not initialized. Call initialize() first.[/]")
            return {}
            
        try:
            time_range_json = json.dumps(time_range) if time_range else ""
            results = self.cpp_wrapper.summarize_logs(time_range_json)
            return json.loads(results)
        except Exception as e:
            self.console.print(f"[bold red]Error summarizing logs:[/] {str(e)}")
            return {}

    def filter_by_time(self, since: Optional[str] = None, until: Optional[str] = None) -> List[Dict[str, Any]]:
        """Filter logs by time range."""
        if not self.is_initialized:
            self.console.print("[bold red]Error: Agent not initialized. Call initialize() first.[/]")
            return []
            
        try:
            results = self.cpp_wrapper.filter_by_time(since or "", until or "")
            return json.loads(results)["logs"]
        except Exception as e:
            self.console.print(f"[bold red]Error filtering logs by time:[/] {str(e)}")
            return []

    def filter_by_level(self, levels: Optional[List[str]] = None, exclude_levels: Optional[List[str]] = None) -> List[Dict[str, Any]]:
        """Filter logs by log level."""
        if not self.is_initialized:
            self.console.print("[bold red]Error: Agent not initialized. Call initialize() first.[/]")
            return []
            
        try:
            results = self.cpp_wrapper.filter_by_level(
                levels or [],
                exclude_levels or []
            )
            return json.loads(results)["logs"]
        except Exception as e:
            self.console.print(f"[bold red]Error filtering logs by level:[/] {str(e)}")
            return []

    def calculate_statistics(self) -> Dict[str, Any]:
        """Calculate statistics about the logs."""
        if not self.is_initialized:
            self.console.print("[bold red]Error: Agent not initialized. Call initialize() first.[/]")
            return {}
            
        try:
            results = self.cpp_wrapper.calculate_statistics()
            return json.loads(results)
        except Exception as e:
            self.console.print(f"[bold red]Error calculating statistics:[/] {str(e)}")
            return {}

    def get_trending_patterns(self, time_window: str = "hour") -> List[Dict[str, Any]]:
        """Detect trending patterns in the logs."""
        if not self.is_initialized:
            self.console.print("[bold red]Error: Agent not initialized. Call initialize() first.[/]")
            return []
            
        try:
            results = self.cpp_wrapper.get_trending_patterns(time_window)
            return json.loads(results)["patterns"]
        except Exception as e:
            self.console.print(f"[bold red]Error detecting trending patterns:[/] {str(e)}")
            return []

def main():
    parser = argparse.ArgumentParser(description="LogAI Agent - AI-powered log analysis")
    parser.add_argument("--log-file", "-f", help="Path to the log file to analyze")
    parser.add_argument("--format", help="Log format (auto-detected if not specified)")
    parser.add_argument("--provider", choices=["openai", "gemini", "claude", "ollama"], default="openai", help="AI provider to use")
    parser.add_argument("--api-key", help="API key for the AI provider")
    parser.add_argument("--model", help="Model to use (provider-specific)")
    parser.add_argument("--host", default="http://localhost:11434", help="Host URL for Ollama (only used with --provider=ollama)")
    
    args = parser.parse_args()
    
    try:
        agent = LogAIAgent(
            provider=args.provider, 
            api_key=args.api_key, 
            model=args.model,
            host=args.host
        )
        
        if args.log_file:
            agent.initialize(args.log_file, args.format)
            
        agent.run_interactive_cli()
        
    except KeyboardInterrupt:
        print("\nExiting LogAI Agent...")
    except Exception as e:
        print(f"Error: {str(e)}")
        sys.exit(1)

if __name__ == "__main__":
    main() 