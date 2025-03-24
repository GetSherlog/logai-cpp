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
    
    def __init__(self, log_file: Optional[str] = None, format: str = "auto"):
        """Initialize the LogAI agent."""
        self.console = Console()
        self.log_file = log_file
        self.format = format
        
        # Initialize the C++ components if available
        if HAS_CPP_MODULE:
            try:
                self.parser = logai_cpp.DrainParser()
                self.template_store = logai_cpp.TemplateStore()
                self.duckdb_store = logai_cpp.DuckDBStore()
                # Add other C++ components as needed
            except Exception as e:
                self.console.print(f"[bold red]Error initializing C++ components: {e}[/bold red]")
                HAS_CPP_MODULE = False
        
        # Initialize LLM based on environment variables
        self.llm_provider = os.environ.get("LLM_PROVIDER", "openai").lower()
        
        # Setup the LLM model
        self.setup_llm()
        
        # Parse log file if provided
        if log_file:
            self.load_log_file(log_file, format)

    def setup_llm(self):
        """Set up the LLM based on environment variables."""
        # Code for setting up LLM remains the same

    def load_log_file(self, file_path: str, format: str = "auto"):
        """Load and parse a log file."""
        self.console.print(f"[bold blue]Loading log file: {file_path}[/bold blue]")
        
        if not os.path.exists(file_path):
            self.console.print(f"[bold red]Error: File not found: {file_path}[/bold red]")
            return False
        
        if not HAS_CPP_MODULE:
            self.console.print("[bold yellow]Warning: C++ module not available. Using limited functionality.[/bold yellow]")
            # Implement fallback behavior if needed
            return False
        
        try:
            # Use C++ module directly to parse log file
            success = logai_cpp.parse_log_file(file_path, format)
            if success:
                self.console.print(f"[bold green]Successfully loaded {file_path}[/bold green]")
            else:
                self.console.print(f"[bold red]Failed to parse log file: {file_path}[/bold red]")
            return success
        except Exception as e:
            self.console.print(f"[bold red]Error parsing log file: {e}[/bold red]")
            return False

    def _register_tools(self):
        """Register all available tools with the agent."""
        
        # Register search_logs tool
        @self.agent.tool
        def search_logs(ctx: RunContext[LogAIDependencies], pattern: str, limit: int = 10) -> Dict[str, Any]:
            """Search for log entries matching specific criteria.
            
            Args:
                pattern: The pattern to search for in the logs
                limit: Maximum number of results to return (default: 10)
            """
            return ctx.deps.cpp_wrapper.search_logs(pattern, limit)
        
        # Register query_db tool
        @self.agent.tool
        def query_db(ctx: RunContext[LogAIDependencies], query: str) -> Dict[str, Any]:
            """Execute SQL queries against log data in DuckDB.
            
            Args:
                query: The SQL query to execute
            """
            return ctx.deps.cpp_wrapper.execute_query(query)
        
        # Register analyze_template tool
        @self.agent.tool
        def analyze_template(ctx: RunContext[LogAIDependencies], template_id: int) -> Dict[str, Any]:
            """Analyze a specific log template and its attributes.
            
            Args:
                template_id: The ID of the template to analyze
            """
            return ctx.deps.cpp_wrapper.get_template(template_id)
        
        # Register get_time_range tool
        @self.agent.tool
        def get_time_range(ctx: RunContext[LogAIDependencies]) -> Dict[str, Any]:
            """Get the time range of logs in the dataset."""
            return ctx.deps.cpp_wrapper.get_time_range()
        
        # Register count_occurrences tool
        @self.agent.tool
        def count_occurrences(
            ctx: RunContext[LogAIDependencies], 
            pattern: str, 
            group_by: Optional[str] = None
        ) -> Dict[str, Any]:
            """Count occurrences of specific patterns or events in logs.
            
            Args:
                pattern: The pattern to count occurrences of
                group_by: Optional field name to group counts by
            """
            return ctx.deps.cpp_wrapper.count_occurrences(pattern, group_by)
        
        # Register filter_by_time tool
        @self.agent.tool
        def filter_by_time(
            ctx: RunContext[LogAIDependencies], 
            since: str = "", 
            until: str = ""
        ) -> Dict[str, Any]:
            """Filter logs by a specific time range using ISO format timestamps.
            
            Args:
                since: Start time in ISO format (YYYY-MM-DDTHH:MM:SS)
                until: End time in ISO format (YYYY-MM-DDTHH:MM:SS)
            """
            return ctx.deps.cpp_wrapper.filter_by_time(since, until)
        
        # Register filter_by_level tool
        @self.agent.tool
        def filter_by_level(
            ctx: RunContext[LogAIDependencies], 
            levels: List[str] = None, 
            exclude_levels: List[str] = None
        ) -> Dict[str, Any]:
            """Filter logs by specific log levels.
            
            Args:
                levels: List of log levels to include (e.g., ERROR, WARN, INFO)
                exclude_levels: List of log levels to exclude
            """
            if levels is None:
                levels = []
            if exclude_levels is None:
                exclude_levels = []
            return ctx.deps.cpp_wrapper.filter_by_level(levels, exclude_levels)
        
        # Register summarize_logs tool
        @self.agent.tool
        def summarize_logs(
            ctx: RunContext[LogAIDependencies],
            time_range: Optional[Dict[str, str]] = None
        ) -> Dict[str, Any]:
            """Generate a summary of log patterns or findings.
            
            Args:
                time_range: Optional dictionary with 'since' and 'until' keys for time filtering
            """
            return ctx.deps.cpp_wrapper.summarize_logs(time_range)
        
        # Register calculate_statistics tool
        @self.agent.tool
        def calculate_statistics(ctx: RunContext[LogAIDependencies]) -> Dict[str, Any]:
            """Calculate comprehensive statistics about the logs.
            
            Computes statistics including time spans, level counts, and field distributions.
            """
            return ctx.deps.cpp_wrapper.calculate_statistics()
        
        # Register get_trending_patterns tool
        @self.agent.tool
        def get_trending_patterns(
            ctx: RunContext[LogAIDependencies],
            time_window: str = "hour"
        ) -> Dict[str, Any]:
            """Detect trending patterns in logs over time.
            
            Args:
                time_window: Time window for trend analysis (e.g., 'hour', 'day', 'minute')
            """
            return ctx.deps.cpp_wrapper.get_trending_patterns(time_window)
        
        # Register filter_by_fields tool
        @self.agent.tool
        def filter_by_fields(
            ctx: RunContext[LogAIDependencies],
            include_fields: List[str] = None,
            exclude_fields: List[str] = None,
            field_pattern: str = ""
        ) -> Dict[str, Any]:
            """Filter logs based on specific fields to include or exclude.
            
            Args:
                include_fields: List of fields to include
                exclude_fields: List of fields to exclude
                field_pattern: Pattern to match in field values
            """
            if include_fields is None:
                include_fields = []
            if exclude_fields is None:
                exclude_fields = []
            return ctx.deps.cpp_wrapper.filter_by_fields(include_fields, exclude_fields, field_pattern)
        
        # Register detect_anomalies tool
        @self.agent.tool
        def detect_anomalies(
            ctx: RunContext[LogAIDependencies],
            threshold: float = 3.0
        ) -> Dict[str, Any]:
            """Detect anomalies in logs, including rare templates and numeric outliers.
            
            Args:
                threshold: Anomaly detection threshold (standard deviations)
            """
            return ctx.deps.cpp_wrapper.detect_anomalies(threshold)
        
        # Register format_logs tool
        @self.agent.tool
        def format_logs(
            ctx: RunContext[LogAIDependencies],
            format: str = "json",
            logs_json: List[str] = None
        ) -> Dict[str, Any]:
            """Format logs in different output formats.
            
            Args:
                format: Output format ('json', 'logfmt', 'csv', 'text')
                logs_json: Optional list of log entries to format
            """
            if logs_json is None:
                logs_json = []
            return ctx.deps.cpp_wrapper.format_logs(format, logs_json)
        
        # Register cluster_logs tool
        @self.agent.tool
        def cluster_logs(
            ctx: RunContext[LogAIDependencies],
            eps: float = 0.5,
            min_samples: int = 5,
            features: List[str] = None
        ) -> Dict[str, Any]:
            """Cluster logs using DBSCAN algorithm based on specified numeric features.
            
            Args:
                eps: Maximum distance between samples for clustering
                min_samples: Minimum number of samples in a cluster
                features: List of numeric features to use for clustering
            """
            if features is None:
                features = ["level"]
            return ctx.deps.cpp_wrapper.cluster_logs(eps, min_samples, features)
        
        # Register visualize_data tool
        @self.agent.tool
        def visualize_data(
            ctx: RunContext[LogAIDependencies],
            data: Dict[str, Any],
            code: str,
            save_path: str = None
        ) -> Dict[str, Any]:
            """Execute Python visualization or analysis code on log data.
            
            Args:
                data: The data to visualize/analyze, typically from another tool's results
                code: Python code for visualization/analysis 
                save_path: Optional path to save the visualization
            """
            import pandas as pd
            import matplotlib.pyplot as plt
            import seaborn as sns
            import numpy as np
            import io
            import base64
            from datetime import datetime
            import os
            
            result = {
                "success": False,
                "image": None,
                "saved_path": None,
                "error": None,
                "data": None
            }
            
            try:
                # Create a temporary directory for visualizations if it doesn't exist
                vis_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "visualizations")
                if not os.path.exists(vis_dir):
                    os.makedirs(vis_dir)
                    
                # Generate a default save path if not provided
                if not save_path:
                    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
                    save_path = os.path.join(vis_dir, f"viz_{timestamp}.png")
                
                # Convert data to pandas DataFrame(s)
                dataframes = {}
                
                if isinstance(data, dict):
                    # Handle different result types
                    if "logs" in data:
                        # Log entries
                        dataframes["logs_df"] = pd.DataFrame(data["logs"])
                    elif "clusters" in data:
                        # Clustering results
                        clusters_list = []
                        for cluster in data["clusters"]:
                            cluster_id = cluster["id"]
                            cluster_size = cluster.get("size", 0)
                            
                            # Create a summary dataframe of clusters
                            if "cluster_summary_df" not in dataframes:
                                dataframes["cluster_summary_df"] = pd.DataFrame(columns=["cluster_id", "size"])
                            
                            cluster_row = pd.DataFrame([{"cluster_id": cluster_id, "size": cluster_size}])
                            dataframes["cluster_summary_df"] = pd.concat([dataframes["cluster_summary_df"], cluster_row])
                            
                            # Create detailed dataframe with all logs by cluster
                            for log in cluster["logs"]:
                                log["cluster_id"] = cluster_id
                                clusters_list.append(log)
                        
                        if clusters_list:
                            dataframes["clusters_df"] = pd.DataFrame(clusters_list)
                    
                    elif "trends" in data:
                        # Trend data
                        dataframes["trends_df"] = pd.DataFrame(data["trends"])
                    
                    elif "breakdown" in data:
                        # Count breakdown
                        dataframes["breakdown_df"] = pd.DataFrame(list(data["breakdown"].items()), 
                                                                columns=["category", "count"])
                    
                    elif "rows" in data and "columns" in data:
                        # SQL query results
                        dataframes["query_df"] = pd.DataFrame(data["rows"], columns=data["columns"])
                    
                    elif "statistics" in data:
                        # Statistics results - process each stat into its own dataframe
                        stats = data["statistics"]
                        
                        if "level_counts" in stats:
                            dataframes["level_df"] = pd.DataFrame(list(stats["level_counts"].items()),
                                                               columns=["level", "count"])
                        
                        if "time_stats" in stats:
                            dataframes["time_df"] = pd.DataFrame([stats["time_stats"]])
                    
                    # Add the raw data as well
                    dataframes["raw_data"] = data
                
                # If no dataframes were created, use a simple dict-to-dataframe conversion
                if not dataframes:
                    # Fallback to simple dataframe
                    try:
                        dataframes["df"] = pd.DataFrame(data)
                    except:
                        result["error"] = "Could not convert data to DataFrame"
                        return result
                
                # Prepare the local variables dictionary with all dataframes and libraries
                local_vars = {
                    # Include all dataframes
                    **dataframes,
                    
                    # Include visualization libraries
                    "pd": pd,
                    "plt": plt,
                    "sns": sns,
                    "np": np,
                    
                    # Include a reference to the original data
                    "data": data,
                    
                    # Include a default figure
                    "fig": plt.figure(figsize=(10, 6)),
                    
                    # Include a variable to store results
                    "results": {}
                }
                
                # Execute the code
                exec(code, {}, local_vars)
                
                # Check if the code created a plot (use the current figure)
                if plt.get_fignums():
                    # Save the figure
                    plt.savefig(save_path, bbox_inches='tight', dpi=300)
                    result["saved_path"] = save_path
                    
                    # Convert to base64 for embedding in response
                    buf = io.BytesIO()
                    plt.savefig(buf, format='png')
                    buf.seek(0)
                    img_str = base64.b64encode(buf.read()).decode('utf-8')
                    result["image"] = img_str
                    
                    plt.close()
                    ctx.deps.console.print(f"[bold green]Visualization saved to:[/] {save_path}")
                
                # Capture any results stored in the results variable
                if "results" in local_vars:
                    result["data"] = local_vars["results"]
                
                result["success"] = True
                return result
                
            except Exception as e:
                import traceback
                result["error"] = str(e)
                result["traceback"] = traceback.format_exc()
                return result
    
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
    
    def process_query(self, query: str) -> str:
        """Process a user query and return the response."""
        if not self.is_initialized:
            return "Error: Please initialize the agent with a log file first."
        
        # Add the query to chat history
        self.chat_history.append({"role": "user", "content": query})
        
        # Create dependencies for the agent run
        deps = LogAIDependencies(
            log_file=self.log_file,
            is_initialized=self.is_initialized,
            cpp_wrapper=self.cpp_wrapper,
            console=self.console
        )
        
        # Run the agent
        try:
            # Call the agent with the user query and dependencies
            result = self.agent.run_sync(
                user_query=query,
                deps=deps
            )
            
            # Display the steps taken
            for step in result.data.steps:
                self.console.print(Panel(Markdown(f"**Thinking:** {step.thought}")))
                self.console.print(f"[bold blue]Using tool:[/] {step.tool}")
                self.console.print(Syntax(json.dumps(step.tool_input, indent=2), "json"))
            
            # Add the final result to chat history
            final_answer = result.data.final_answer
            self.chat_history.append({"role": "assistant", "content": final_answer})
            
            return final_answer
            
        except Exception as e:
            error_msg = f"Error processing query: {str(e)}"
            self.console.print(f"[bold red]{error_msg}[/]")
            return error_msg
    
    def run_interactive_cli(self) -> None:
        """Run the interactive CLI."""
        self.console.print(Panel.fit(
            "[bold blue]LogAI Agent[/] - Ask questions about your logs",
            title="Welcome",
            border_style="blue"
        ))
        
        if not self.is_initialized:
            log_file = Prompt.ask("[bold yellow]Enter the path to your log file[/]")
            format_opt = Prompt.ask("[bold yellow]Log format (leave empty for auto-detection)[/]", default="")
            
            if not self.initialize(log_file, format_opt):
                self.console.print("[bold red]Failed to initialize. Exiting.[/]")
                return
        
        self.console.print("\n[bold green]Ready to answer questions about your logs![/]")
        self.console.print("Type 'exit' or 'quit' to end the session.")
        
        while True:
            query = Prompt.ask("\n[bold blue]Ask a question about your logs[/]")
            
            if query.lower() in ("exit", "quit"):
                self.console.print("[bold blue]Goodbye![/]")
                break
                
            start_time = time.time()
            response = self.process_query(query)
            end_time = time.time()
            
            self.console.print(Panel(
                Markdown(response),
                title="Answer",
                border_style="green"
            ))
            
            self.console.print(f"[dim]Response time: {end_time - start_time:.2f} seconds[/]")
    
    def _build_system_prompt(self) -> str:
        """Build the system prompt for the agent."""
        return f"""You are LogAI Agent, an AI assistant specialized in analyzing logs.
            
Current log file: {self.log_file or "None"}

To solve the user's question:
1. Break down the question into steps
2. For each step, decide which tool to use
3. Call the tool with the right parameters
4. Interpret the results
5. Provide a clear, concise answer to the user's question

Always be factual and base your answers on the data from the tools.
"""

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