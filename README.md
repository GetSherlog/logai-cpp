# LogAI C++

A C++ library for log analysis with Python bindings.

## Overview

LogAI C++ provides tools for log analysis, template extraction, clustering, and anomaly detection. It includes:

- Drain parser for log template extraction
- Vector embedding generation
- Clustering algorithms (DBSCAN)
- Anomaly detection (One-Class SVM)
- DuckDB integration for storage and queries
- Direct Python bindings for cross-platform usage

## Simplified Architecture

This project uses a streamlined approach:

1. C++ core library for high-performance log processing
2. Direct Python bindings using pybind11 (no wrapper layer)
3. Python CLI tool that uses the bindings directly
4. Docker-based build system that creates a portable wheel package

This architecture maximizes performance by eliminating unnecessary layers between the Python CLI and C++ core.

## Quick Start

### Build and Install

Simply run the build script:

```bash
./build_and_run.sh
```

This script will:
1. Build the Docker image with the C++ library and Python bindings
2. Extract the wheel package to the `./wheels` directory
3. Provide instructions for installing and using the CLI

### Using the Python CLI on Your Host Machine

After the build completes:

1. Install the wheel package:
   ```bash
   pip install ./wheels/logai_cpp-*.whl
   ```

2. Configure your LLM provider:

   **For OpenAI (default):**
   ```bash
   export LLM_PROVIDER=openai
   export OPENAI_API_KEY=your_api_key_here
   export OPENAI_MODEL=gpt-4o  # Optional, defaults to gpt-4o
   ```

   **For Google Gemini:**
   ```bash
   export LLM_PROVIDER=gemini
   export GEMINI_API_KEY=your_api_key_here
   export GEMINI_MODEL=gemini-pro  # Optional
   ```

   **For Ollama (local LLMs):**
   ```bash
   export LLM_PROVIDER=ollama
   export OLLAMA_ENDPOINT=http://localhost:11434/api/generate
   export OLLAMA_MODEL=llama3  # Specify your Ollama model
   ```

3. Run the CLI tool:
   ```bash
   logai-agent --log-file path/to/your/logfile.log
   ```

## Example Commands

```bash
# Extract log templates
logai-agent extract --log-file logs/example.log

# Cluster logs
logai-agent cluster --log-file logs/example.log

# Detect anomalies
logai-agent anomaly --log-file logs/example.log

# Interactive analysis
logai-agent analyze --log-file logs/example.log
```

## Docker Development Environment (Optional)

If you want to run the tools inside a Docker container for development:

```bash
docker run -it --name logai-cpp-container \
  -v "$(pwd):/workspace" \
  -v "$(pwd)/logs:/workspace/logs" \
  -v "$(pwd)/uploads:/workspace/uploads" \
  -e OPENAI_API_KEY="${OPENAI_API_KEY}" \
  -e LLM_PROVIDER="${LLM_PROVIDER}" \
  -e GEMINI_API_KEY="${GEMINI_API_KEY}" \
  -e OLLAMA_ENDPOINT="${OLLAMA_ENDPOINT}" \
  -e OLLAMA_MODEL="${OLLAMA_MODEL}" \
  logai-cpp
```

## Python Integration

The Python module `logai_cpp` provides direct bindings to the C++ library with no intermediate wrapper layer, ensuring maximum performance. See the `python/examples` directory for sample code. 