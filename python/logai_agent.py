#!/usr/bin/env python3
import sys
import json
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
        
        try:
            # Create DuckDB connection
            import duckdb
            self.duckdb_conn = duckdb.connect('logai.db')
            
            # Create tables if they don't exist
            self.duckdb_conn.execute("""
                CREATE TABLE IF NOT EXISTS log_entries (
                    id INTEGER PRIMARY KEY,
                    timestamp VARCHAR,
                    level VARCHAR,
                    message VARCHAR,
                    template_id VARCHAR
                )
            """)
            
            self.duckdb_conn.execute("""
                CREATE TABLE IF NOT EXISTS log_templates (
                    template_id VARCHAR PRIMARY KEY,
                    template TEXT,
                    count INTEGER
                )
            """)
            
            # Parse the log file using the C++ wrapper
            parsed_logs = self.cpp_wrapper.parse_log_file(log_file, format or "")
            if not parsed_logs:
                self.console.print("[bold red]Failed to parse log file.[/]")
                return False
            
            # Load the parsed logs into DuckDB
            self._load_logs_to_duckdb(parsed_logs)
            
            # Extract templates
            templates = self._extract_templates_from_logs(parsed_logs)
            
            # Store templates in DuckDB
            self._store_templates_in_duckdb(templates)
            
            # Store templates in Milvus if available
            try:
                self._store_templates_in_milvus(templates)
            except Exception as e:
                self.console.print(f"[bold yellow]Warning: Failed to store templates in Milvus: {str(e)}[/]")
            
            self.log_file = log_file
            self.is_initialized = True
            
            self.console.print("[bold green]✓[/] Log file parsed successfully")
            self.console.print("[bold green]✓[/] Templates extracted and stored")
            self.console.print("[bold green]✓[/] Attributes stored in DuckDB")
            
            return True
        except Exception as e:
            self.console.print(f"[bold red]Error initializing agent:[/] {str(e)}")
            return False

    def _load_logs_to_duckdb(self, parsed_logs: List[Dict[str, Any]]) -> None:
        """Load parsed logs into DuckDB."""
        # Create a Pandas DataFrame from the parsed logs
        import pandas as pd
        
        # Extract relevant fields
        records = []
        for i, log in enumerate(parsed_logs):
            record = {
                'id': i,
                'timestamp': log.get('timestamp'),
                'level': log.get('level'),
                'message': log.get('message'),
                'template_id': log.get('template_id', ''),
                'body': log.get('body')
            }
            records.append(record)
        
        # Create DataFrame
        df = pd.DataFrame(records)
        
        # Insert into DuckDB
        self.duckdb_conn.execute("DELETE FROM log_entries")
        self.duckdb_conn.execute("INSERT INTO log_entries SELECT * FROM df")
        
        self.console.print(f"[bold green]✓[/] Loaded {len(records)} log entries into DuckDB")

    def _extract_templates_from_logs(self, parsed_logs: List[Dict[str, Any]]) -> Dict[str, Dict[str, Any]]:
        """Extract templates from parsed logs."""
        templates = {}
        
        # Count occurrences of each template
        for log in parsed_logs:
            template = log.get('template', '')
            if not template:
                continue
            
            template_id = str(hash(template) % 10000000)
            
            if template_id in templates:
                templates[template_id]['count'] += 1
            else:
                templates[template_id] = {
                    'template_id': template_id,
                    'template': template,
                    'count': 1
                }
        
        return templates

    def _store_templates_in_duckdb(self, templates: Dict[str, Dict[str, Any]]) -> None:
        """Store templates in DuckDB."""
        import pandas as pd
        
        # Create DataFrame from templates
        template_records = list(templates.values())
        df = pd.DataFrame(template_records)
        
        # Insert into DuckDB
        self.duckdb_conn.execute("DELETE FROM log_templates")
        self.duckdb_conn.execute("INSERT INTO log_templates SELECT * FROM df")
        
        self.console.print(f"[bold green]✓[/] Stored {len(template_records)} templates in DuckDB")

    def _store_templates_in_milvus(self, templates: Dict[str, Dict[str, Any]]) -> None:
        """Store templates in Milvus."""
        # Initialize Milvus connection
        if not self.cpp_wrapper.init_milvus():
            raise Exception("Failed to initialize Milvus connection")
        
        # Store each template in Milvus
        stored_count = 0
        failed_count = 0
        
        for template_id, template_data in templates.items():
            # Generate embedding
            template_text = template_data['template']
            embedding = self.cpp_wrapper.generate_template_embedding(template_text)
            
            if not embedding:
                failed_count += 1
                continue
            
            # Store in Milvus using the Python Milvus client
            success = self.cpp_wrapper.insert_template(
                template_id=template_id,
                template=template_text,
                count=template_data['count'],
                embedding=embedding
            )
            
            if success:
                stored_count += 1
            else:
                failed_count += 1
        
        self.console.print(f"[bold green]✓[/] Stored {stored_count} templates in Milvus, failed: {failed_count}")

    def execute_query(self, query: str) -> Dict[str, Any]:
        """Execute a SQL query against the log data."""
        if not self.is_initialized:
            self.console.print("[bold red]Error: Agent not initialized. Call initialize() first.[/]")
            return {"columns": [], "rows": []}
        
        try:
            # Execute query directly in DuckDB
            result = self.duckdb_conn.execute(query).fetchdf()
            
            # Convert to dictionary format
            columns = result.columns.tolist()
            rows = result.values.tolist()
            
            return {
                "columns": columns,
                "rows": rows
            }
        except Exception as e:
            self.console.print(f"[bold red]Error executing query:[/] {str(e)}")
            return {"columns": [], "rows": []}

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