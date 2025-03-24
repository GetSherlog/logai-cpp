#!/usr/bin/env python3
import os
import sys
import json
import time
import argparse
from typing import List, Dict, Any, Optional, Union, Callable
from pydantic import BaseModel, Field

# Import the Pydantic AI agent framework
from instructor import OpenAISchema
from instructor import Instructor
import openai

# Helper for CLI styling
from rich.console import Console
from rich.markdown import Markdown
from rich.panel import Panel
from rich.prompt import Prompt
from rich.syntax import Syntax

# Import our C++ wrapper
from logai_cpp_wrapper import get_wrapper

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

# Define the agent's thought process

class LogAnalysisStep(OpenAISchema):
    """A step in the log analysis process."""
    thought: str = Field(..., description="The agent's thinking process for this step")
    tool: str = Field(..., description="The tool to use for this step")
    tool_input: Dict[str, Any] = Field(..., description="The input parameters for the tool")

class LogAnalysisResult(OpenAISchema):
    """The final result of log analysis."""
    steps: List[LogAnalysisStep] = Field(..., description="Steps taken to analyze the logs")
    final_answer: str = Field(..., description="The final answer to the user's question")

# Tool definitions

class LogAITool:
    """Base class for all LogAI tools."""
    name: str
    description: str
    
    def __init__(self, name: str, description: str, func: Callable):
        self.name = name
        self.description = description
        self.func = func
        
    def __call__(self, **kwargs):
        return self.func(**kwargs)

# Main Agent class

class LogAIAgent:
    """AI Agent for log analysis using the LogAI C++ tools."""
    
    def __init__(self, api_key: Optional[str] = None, model: str = "gpt-4o"):
        """Initialize the LogAI Agent."""
        self.console = Console()
        
        # Set up OpenAI
        if api_key:
            openai.api_key = api_key
        else:
            openai.api_key = os.environ.get("OPENAI_API_KEY")
            
        if not openai.api_key:
            raise ValueError("OpenAI API key is required. Set OPENAI_API_KEY environment variable or pass it as an argument.")
        
        self.model = model
        self.client = Instructor(openai.OpenAI())
        
        # Get the C++ wrapper
        self.cpp_wrapper = get_wrapper()
        
        # Initialize state
        self.log_file = None
        self.templates = []
        self.is_initialized = False
        self.chat_history = []
        
        # Register tools
        self.tools = self._register_tools()
    
    def _register_tools(self) -> Dict[str, LogAITool]:
        """Register all available tools."""
        tools = {}
        
        # Search logs tool
        tools["search_logs"] = LogAITool(
            name="search_logs",
            description="Search for log entries matching specific criteria",
            func=self._tool_search_logs
        )
        
        # Query DB tool
        tools["query_db"] = LogAITool(
            name="query_db",
            description="Execute SQL queries against log data in DuckDB",
            func=self._tool_query_db
        )
        
        # Analyze template tool
        tools["analyze_template"] = LogAITool(
            name="analyze_template",
            description="Analyze a specific log template and its attributes",
            func=self._tool_analyze_template
        )
        
        # Get time range tool
        tools["get_time_range"] = LogAITool(
            name="get_time_range",
            description="Get the time range of logs in the dataset",
            func=self._tool_get_time_range
        )
        
        # Count occurrences tool
        tools["count_occurrences"] = LogAITool(
            name="count_occurrences",
            description="Count occurrences of specific patterns or events in logs",
            func=self._tool_count_occurrences
        )
        
        # Summarize logs tool
        tools["summarize_logs"] = LogAITool(
            name="summarize_logs",
            description="Generate a summary of log patterns or findings",
            func=self._tool_summarize_logs
        )
        
        # Filter by time tool
        tools["filter_by_time"] = LogAITool(
            name="filter_by_time",
            description="Filter logs by a specific time range using ISO format timestamps (YYYY-MM-DDTHH:MM:SS)",
            func=self._tool_filter_by_time
        )
        
        # Filter by log level tool
        tools["filter_by_level"] = LogAITool(
            name="filter_by_level",
            description="Filter logs by specific log levels (e.g., ERROR, WARN, INFO)",
            func=self._tool_filter_by_level
        )
        
        # Calculate statistics tool
        tools["calculate_statistics"] = LogAITool(
            name="calculate_statistics",
            description="Calculate comprehensive statistics about the logs, including time spans, level counts, and field distributions",
            func=self._tool_calculate_statistics
        )
        
        # Get trending patterns tool
        tools["get_trending_patterns"] = LogAITool(
            name="get_trending_patterns",
            description="Detect trending patterns in logs over time, identifying significant changes in log frequencies",
            func=self._tool_get_trending_patterns
        )
        
        # Filter by fields tool
        tools["filter_by_fields"] = LogAITool(
            name="filter_by_fields",
            description="Filter logs based on specific fields to include or exclude",
            func=self._tool_filter_by_fields
        )
        
        # Detect anomalies tool
        tools["detect_anomalies"] = LogAITool(
            name="detect_anomalies",
            description="Detect anomalies in logs, including rare templates and numeric outliers",
            func=self._tool_detect_anomalies
        )
        
        # Format logs tool
        tools["format_logs"] = LogAITool(
            name="format_logs",
            description="Format logs in different output formats (json, logfmt, csv, text)",
            func=self._tool_format_logs
        )
        
        # Cluster logs tool
        tools["cluster_logs"] = LogAITool(
            name="cluster_logs",
            description="Cluster logs using DBSCAN algorithm based on specified numeric features",
            func=self._tool_cluster_logs
        )
        
        # Visualize data tool
        tools["visualize_data"] = LogAITool(
            name="visualize_data",
            description="Execute Python visualization or analysis code on log data",
            func=self._tool_visualize_data
        )
        
        return tools
    
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
        
        # Generate the tool call sequence using the LLM
        try:
            analysis = self.client.chat.completions.create(
                model=self.model,
                response_model=LogAnalysisResult,
                messages=[
                    {"role": "system", "content": self._build_system_prompt()},
                    *self.chat_history
                ]
            )
            
            # Execute each step
            results = []
            for step in analysis.steps:
                self.console.print(Panel(Markdown(f"**Thinking:** {step.thought}")))
                
                if step.tool in self.tools:
                    tool_result = self.tools[step.tool](**step.tool_input)
                    results.append({"tool": step.tool, "result": tool_result})
                    
                    # Display intermediate results
                    self.console.print(f"[bold blue]Using tool:[/] {step.tool}")
                    self.console.print(Syntax(json.dumps(tool_result, indent=2), "json"))
                else:
                    self.console.print(f"[bold red]Unknown tool:[/] {step.tool}")
            
            # Add the final result to chat history
            result = analysis.final_answer
            self.chat_history.append({"role": "assistant", "content": result})
            
            return result
            
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
        tools_json = {}
        for name, tool in self.tools.items():
            tools_json[name] = {
                "description": tool.description
            }
        
        return f"""You are LogAI Agent, an AI assistant specialized in analyzing logs.
            
Current log file: {self.log_file or "None"}

You have access to the following tools:
{json.dumps(tools_json, indent=2)}

To solve the user's question:
1. Break down the question into steps
2. For each step, decide which tool to use
3. Call the tool with the right parameters
4. Interpret the results
5. Provide a clear, concise answer to the user's question

Always be factual and base your answers on the data from the tools.
"""
    
    # Tool implementations using C++ wrapper
    
    def _tool_search_logs(self, pattern: str, limit: int = 10) -> Dict[str, Any]:
        """Search for logs matching a pattern."""
        return self.cpp_wrapper.search_logs(pattern, limit)
    
    def _tool_query_db(self, query: str) -> Dict[str, Any]:
        """Execute a SQL query against the logs database."""
        return self.cpp_wrapper.execute_query(query)
    
    def _tool_analyze_template(self, template_id: int) -> Dict[str, Any]:
        """Analyze a specific log template."""
        return self.cpp_wrapper.get_template(template_id)
    
    def _tool_get_time_range(self) -> Dict[str, Any]:
        """Get the time range of logs in the dataset."""
        return self.cpp_wrapper.get_time_range()
    
    def _tool_count_occurrences(self, pattern: str, group_by: Optional[str] = None) -> Dict[str, Any]:
        """Count occurrences of specific patterns in logs."""
        return self.cpp_wrapper.count_occurrences(pattern, group_by)
    
    def _tool_summarize_logs(self, time_range: Optional[Dict[str, str]] = None) -> Dict[str, Any]:
        """Generate a summary of logs."""
        return self.cpp_wrapper.summarize_logs(time_range)
    
    # Additional tool implementations
    
    def _tool_filter_by_time(self, since: str = "", until: str = "") -> Dict[str, Any]:
        """Filter logs by time range."""
        return self.cpp_wrapper.filter_by_time(since, until)
    
    def _tool_filter_by_level(self, levels: List[str] = None, exclude_levels: List[str] = None) -> Dict[str, Any]:
        """Filter logs by log level."""
        if levels is None:
            levels = []
        if exclude_levels is None:
            exclude_levels = []
        return self.cpp_wrapper.filter_by_level(levels, exclude_levels)
    
    def _tool_calculate_statistics(self) -> Dict[str, Any]:
        """Calculate statistics about the logs."""
        return self.cpp_wrapper.calculate_statistics()
    
    def _tool_get_trending_patterns(self, time_window: str = "hour") -> Dict[str, Any]:
        """Detect trending patterns in logs."""
        return self.cpp_wrapper.get_trending_patterns(time_window)
    
    def _tool_filter_by_fields(self, include_fields: List[str] = None, 
                              exclude_fields: List[str] = None,
                              field_pattern: str = "") -> Dict[str, Any]:
        """Filter logs by fields."""
        if include_fields is None:
            include_fields = []
        if exclude_fields is None:
            exclude_fields = []
        return self.cpp_wrapper.filter_by_fields(include_fields, exclude_fields, field_pattern)
    
    def _tool_detect_anomalies(self, threshold: float = 3.0) -> Dict[str, Any]:
        """Detect anomalies in logs."""
        return self.cpp_wrapper.detect_anomalies(threshold)
    
    def _tool_format_logs(self, format: str = "json", logs_json: List[str] = None) -> Dict[str, Any]:
        """Format logs in different output formats."""
        if logs_json is None:
            logs_json = []
        return self.cpp_wrapper.format_logs(format, logs_json)
    
    def _tool_cluster_logs(self, eps: float = 0.5, min_samples: int = 5, features: List[str] = None) -> Dict[str, Any]:
        """Cluster logs using DBSCAN algorithm."""
        if features is None:
            features = ["level"]
        return self.cpp_wrapper.cluster_logs(eps, min_samples, features)
    
    def _tool_visualize_data(self, data: Dict[str, Any], code: str, save_path: str = None) -> Dict[str, Any]:
        """Execute Python visualization or analysis code on log data.
        
        Args:
            data: The data to visualize/analyze, typically from another tool's results
            code: Python code for visualization/analysis generated by the LLM
            save_path: Optional path to save the visualization
            
        Returns:
            Dict with execution results
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
                self.console.print(f"[bold green]Visualization saved to:[/] {save_path}")
            
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

def main():
    parser = argparse.ArgumentParser(description="LogAI Agent - AI-powered log analysis")
    parser.add_argument("--log-file", "-f", help="Path to the log file to analyze")
    parser.add_argument("--format", help="Log format (auto-detected if not specified)")
    parser.add_argument("--api-key", help="OpenAI API key (can also use OPENAI_API_KEY env var)")
    parser.add_argument("--model", default="gpt-4o", help="OpenAI model to use")
    
    args = parser.parse_args()
    
    try:
        agent = LogAIAgent(api_key=args.api_key, model=args.model)
        
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