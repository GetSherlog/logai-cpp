# LogAI Agent

An AI-powered agent for advanced log analysis, combining C++ performance with the flexibility of Python and the intelligence of LLMs.

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