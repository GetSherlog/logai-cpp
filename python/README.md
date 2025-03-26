# LogAI Python Module

This Python module provides a high-level interface to the LogAI C++ library.

## Architecture

The LogAI Python module is designed as a direct interface to the high-performance C++ components:

1. **C++ Core**: 
   - Fast log parsing with customizable parsers
   - Template extraction using the Drain algorithm
   - High-performance data storage and querying with DuckDB
   - Vector embeddings and clustering capabilities

2. **Python Interface**:
   - Direct binding to the C++ library using pybind11
   - Data manipulation with pandas integration
   - AI-powered analysis capabilities
   - CLI for interactive log analysis

## Components

- **DuckDB Store**: Efficient SQL-based storage and querying of log data
- **Drain Parser**: Extract log templates from raw logs
- **Template Store**: Manage and search log templates
- **Vector Embeddings**: Generate and manage embeddings for logs
- **Clustering**: Find patterns in log data

## Getting Started

```python
import logai_cpp

# Initialize components
parser = logai_cpp.DrainParser()
template_store = logai_cpp.TemplateStore()
duckdb_store = logai_cpp.DuckDBStore()

# Parse a log file
parsed_logs = parser.parse_file("path/to/logs.log")

# Extract templates
for log in parsed_logs:
    template_id = log["template_id"]
    template_str = log["template"]
    template_store.add_template(template_id, template_str, log)

# Create a table in DuckDB for efficient querying
columns = ["timestamp", "level", "component", "message"]
types = ["VARCHAR", "VARCHAR", "VARCHAR", "VARCHAR"]
duckdb_store.init_template_table("template1", columns, types)

# Query logs
results = duckdb_store.execute_query("SELECT * FROM template1 WHERE level = 'ERROR'")

# Convert to pandas DataFrame for analysis
import pandas as pd
df = pd.DataFrame(results[1:], columns=results[0])
```

## Using with pandas

```python
from logai_cpp import DrainParser, DuckDBStore
import pandas as pd

# Initialize LogAI components
parser = DrainParser()
store = DuckDBStore()

# Parse logs
parsed_logs = parser.parse_file("logs.log")

# Set up DuckDB table
columns = ["timestamp", "level", "message"]
types = ["VARCHAR", "VARCHAR", "VARCHAR"]
store.init_template_table("logs", columns, types)

# Execute a query and convert to pandas DataFrame
query = "SELECT * FROM logs WHERE level = 'ERROR'"
results = store.execute_query(query)
headers = results[0]
data = results[1:]
df = pd.DataFrame(data, columns=headers)

# Now use pandas for further analysis
print(df.describe())
```

## Running the CLI

```bash
# Basic usage
logai-agent --log-file path/to/logs.log

# Specify log format
logai-agent --log-file path/to/logs.log --format json

# Run analysis
logai-agent analyze --log-file path/to/logs.log
```

## Features

- Parse and analyze log files of various formats
- Extract log templates and store them in Milvus for vector search
- Store structured log attributes in DuckDB for SQL analysis
- Provide a natural language interface for asking complex questions about logs
- Support for multi-step reasoning to solve complex log analysis tasks

## Installation

### Prerequisites

- Python 3.8+
- C++ compiler (GCC 9+, Clang 10+, or MSVC 2019+)
- CMake 3.14+
- OpenAI API key

### Installing from source

1. Clone the repository:
   ```
   git clone https://github.com/your-org/logai-cpp.git
   cd logai-cpp
   ```

2. Set up the Python environment:
   ```
   cd python
   python -m venv venv
   source venv/bin/activate  # On Windows: venv\Scripts\activate
   pip install -e .
   ```

This will build the C++ extension module and install the Python package.

## Usage

### Command Line Interface

```bash
# Set your OpenAI API key
export OPENAI_API_KEY=your-api-key

# Run the agent with a log file
logai-agent --log-file path/to/your/logfile.log

# Specify the log format if needed
logai-agent --log-file path/to/your/logfile.log --format jsonl

# Use a different model
logai-agent --log-file path/to/your/logfile.log --model gpt-4
```

### Interactive Mode

Once the agent is running, you can ask questions about your logs:

```
Ask a question about your logs: How many errors occurred in the last hour?
```

The agent will:
1. Break down the question into steps
2. Search for relevant log entries
3. Process the data
4. Provide a comprehensive answer

### Example Questions

- How many errors occurred in the last hour?
- What are the most common error types?
- Show me all logs related to user X
- When did the system last restart?
- Was there a spike in connection drops between 2pm and 3pm?
- Is there a correlation between high CPU usage and error rates?

## Development

### Project Structure

- `python/` - Python package directory
  - `logai_agent.py` - Main agent implementation
  - `logai_cpp_wrapper.py` - Wrapper for C++ functionality
  - `setup.py` - Build script for the Python package
- `src/` - C++ source code
  - `python_bindings.cpp` - pybind11 bindings for C++ code

### Adding New Tools

To add a new tool to the agent:

1. Add the tool implementation in `logai_cpp_wrapper.py`
2. Add the C++ implementation in `src/`
3. Expose it via pybind11 in `python_bindings.cpp`
4. Register the tool in the `_register_tools` method in `logai_agent.py`

## License

MIT 