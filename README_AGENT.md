# LogAI Agent with Pydantic-AI

This module provides a powerful AI agent for log analysis that can work with multiple LLM providers using the Pydantic-AI framework.

## Features

- **Multiple AI Providers**: Works with OpenAI, Google Gemini, Anthropic Claude, or Ollama
- **Structured Output**: Uses Pydantic models to validate and structure agent outputs
- **Tool-Based Architecture**: Provides specialized tools for log analysis tasks
- **Interactive CLI**: User-friendly command-line interface for querying logs
- **Type-Safe Design**: Leverages Python's type system for reliable code

## Installation

1. Install the required dependencies:

```bash
pip install -r requirements.txt
```

2. Set up your API key(s) based on which provider you want to use:

- **OpenAI**: Set `OPENAI_API_KEY` environment variable
- **Google Gemini**: Set `GEMINI_API_KEY` environment variable 
- **Anthropic Claude**: Set `ANTHROPIC_API_KEY` environment variable
- **Ollama**: Make sure Ollama is running on your local machine or a remote server

## Usage

### Basic Usage with Command Line Interface

```bash
# Using OpenAI (default)
python python/logai_agent.py --log-file /path/to/your/logs.log

# Using Google Gemini
python python/logai_agent.py --log-file /path/to/your/logs.log --provider gemini

# Using Anthropic Claude
python python/logai_agent.py --log-file /path/to/your/logs.log --provider claude

# Using Ollama with a specific model
python python/logai_agent.py --log-file /path/to/your/logs.log --provider ollama --model llama3 --host http://localhost:11434
```

### Using in Python Code

```python
from logai_agent import LogAIAgent

# Create an agent with your preferred provider
agent = LogAIAgent(provider="openai")  # or "gemini", "claude", "ollama"

# Initialize with a log file
agent.initialize("path/to/your/logs.log")

# Process a query
result = agent.process_query("What are the most common error patterns in these logs?")
print(result)
```

See `python/example_usage.py` for more detailed examples.

## Available Tools

The LogAI agent provides a comprehensive set of tools for log analysis:

### Basic Analysis Tools
1. **search_logs**: Search for log entries matching specific criteria
2. **query_db**: Execute SQL queries against log data in DuckDB
3. **analyze_template**: Analyze a specific log template and its attributes
4. **get_time_range**: Get the time range of logs in the dataset

### Filtering Tools
5. **filter_by_time**: Filter logs by a specific time range using ISO format timestamps
6. **filter_by_level**: Filter logs by specific log levels (e.g., ERROR, WARN, INFO)
7. **filter_by_fields**: Filter logs based on specific fields to include or exclude

### Statistical Analysis Tools
8. **count_occurrences**: Count occurrences of specific patterns or events in logs
9. **calculate_statistics**: Calculate comprehensive statistics about logs
10. **get_trending_patterns**: Detect trending patterns in logs over time

### Advanced Analysis Tools
11. **summarize_logs**: Generate a summary of log patterns or findings
12. **cluster_logs**: Cluster logs using DBSCAN algorithm based on numeric features
13. **detect_anomalies**: Detect anomalies in logs, including rare templates and numeric outliers

### Visualization and Formatting
14. **visualize_data**: Execute Python visualization code on log data
15. **format_logs**: Format logs in different output formats (json, logfmt, csv, text)

## Architecture

The LogAI agent uses Pydantic-AI, a Python agent framework designed for building production-grade AI applications. Key components include:

- **LogAIAgent**: The main class that handles initialization and user interaction
- **LogAIDependencies**: A dataclass for dependency injection of resources to tools
- **LogAnalysisResult**: A Pydantic model that defines the structure of agent responses
- **LogAnalysisStep**: A Pydantic model that captures each reasoning step taken by the agent

The agent communicates with the LogAI C++ library through a wrapper interface to perform log parsing, analysis, and querying operations.

## Customization

You can customize the agent by:

1. Adding new tools to the `_register_tools` method in `LogAIAgent`
2. Modifying the system prompt in `_build_system_prompt`
3. Implementing additional LLM providers (see Pydantic-AI documentation)

## Troubleshooting

- **API Key Issues**: Ensure your API keys are set correctly in environment variables or passed explicitly to the agent
- **Dependency Errors**: Make sure all required Python packages are installed
- **Ollama Connection**: Check that Ollama is running and accessible at the specified host URL
- **Log Parsing Failures**: Verify that your log file format is supported by the LogAI C++ library

## License

[MIT License](LICENSE) 