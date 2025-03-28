# LogAI C++

A high-performance C++ library for log analysis, focusing on template extraction and storage. This module is responsible for:

- Log parsing and template extraction using DRAIN algorithm
- Template storage in Milvus for efficient similarity search
- Log attribute storage in DuckDB for structured querying

The C++ module is designed to be fast and efficient, handling the core responsibilities of log parsing and storage. Higher-level analysis features are handled by the Python AI agent.

## Features

- High-performance log parsing
- Template extraction using DRAIN algorithm
- Template storage in Milvus for vector similarity search
- Structured log attribute storage in DuckDB
- Python bindings for easy integration
- FastAPI server for API access to log analysis functionality

## Dependencies

- C++17 or later
- CMake 3.15 or later
- DuckDB
- Milvus
- Folly
- nlohmann/json
- pybind11 (for Python bindings)

## Building

```bash
# Clone the repository
git clone https://github.com/yourusername/logai-cpp.git
cd logai-cpp

# Create build directory
mkdir build && cd build

# Configure with CMake
cmake ..

# Build
make -j8
```

## Running with Docker

The easiest way to run LogAI is using Docker Compose:

```bash
# Copy the example environment file and modify as needed
cp .env.example .env

# Edit .env and add your API keys
nano .env

# Start the services
docker-compose up -d
```

This will start:
- The LogAI FastAPI server on port 8000
- Milvus vector database
- Required supporting services

## API Usage

Once the server is running, you can access the OpenAPI documentation at:

```
http://localhost:8000/docs
```

Key API endpoints:

- `POST /api/configure` - Configure the AI agent
- `POST /api/initialize` - Initialize with a log file
- `POST /api/upload-log-file` - Upload and parse a log file
- `POST /api/search` - Search logs with a pattern
- `POST /api/execute-query` - Execute SQL queries against log data
- `GET /api/statistics` - Get statistics about the loaded logs

## Python Integration

The C++ module provides Python bindings for easy integration:

```python
import logai_cpp

# Parse a log file
logai_cpp.parse_log_file("logs/example.log")

# Extract templates
templates = logai_cpp.extract_templates()

# Store templates in Milvus
logai_cpp.store_templates_in_milvus()

# Store attributes in DuckDB
logai_cpp.store_attributes_in_duckdb()
```

### Using the FastAPI client

You can also use the REST API from any programming language:

```python
import requests

# Configure the agent
response = requests.post("http://localhost:8000/api/configure", json={
    "provider": "openai",
    "api_key": "your-api-key"
})

# Initialize with a log file
response = requests.post("http://localhost:8000/api/initialize", json={
    "log_file": "/path/to/logfile.log"
})

# Search logs
response = requests.post("http://localhost:8000/api/search", json={
    "query": "error",
    "limit": 10
})
```

## License

MIT License 