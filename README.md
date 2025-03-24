# LogAI-CPP: High-Performance Log Analysis Engine with AI

LogAI-CPP is a high-performance log analysis library written in C++ with Python bindings, integrating AI capabilities for advanced log analysis, visualization, and insights.

## Features

- **High-Performance C++ Core**: Efficiently process and analyze large log files using optimized C++ code
- **Python Integration**: Seamless Python bindings that provide the best of both worlds
- **AI-Powered Analysis**: Intelligent log analysis using LLMs (OpenAI GPT-4o by default)
- **Data Visualization**: Generate insightful visualizations from log data
- **Clustering**: Group similar logs using DBSCAN clustering
- **Template Extraction**: Extract log templates using the DRAIN algorithm
- **SQL Queries**: Run SQL queries against log data using DuckDB integration
- **Vector Search**: Find similar log patterns using vector search techniques
- **Time Analysis**: Analyze logs over time ranges and detect trends
- **Anomaly Detection**: Identify unusual patterns and outliers in logs

## Quick Start

### Building LogAI-CPP

We provide a Docker-based build script that compiles the C++ extension for your target platform:

```bash
# Make the build script executable
chmod +x build_and_run.sh

# Run the build script
./build_and_run.sh
```

Follow the prompts to select your target platform. The script will build the C++ extension in Docker and provide instructions for running the LogAI agent.

### Using the LogAI Agent CLI

1. Set your OpenAI API key:
   ```bash
   export OPENAI_API_KEY=your_api_key_here
   ```

2. Run the CLI with a log file:
   ```bash
   python python/logai_agent_cli.py --log-file path/to/your/logfile.log
   ```

3. Use the interactive CLI to ask questions about your log data:
   - "Show me all error logs from the last 24 hours"
   - "Find patterns of failed connection attempts"
   - "Visualize the distribution of log levels"
   - "Cluster logs by response time and analyze outliers"

## Example Scripts

We provide several example scripts to demonstrate LogAI's capabilities:

```bash
# Basic examples
python python/examples/connection_drops.py
python python/examples/performance_patterns.py
python python/examples/interactive_query.py

# Advanced features
python python/examples/advanced_features.py
python python/examples/log_clustering.py
python python/examples/visualization_example.py
```

## Log Analysis and Visualization

LogAI can generate powerful visualizations from log data:

1. **Log Level Distribution**:
   - Bar charts showing distribution of log levels
   - Percentage of ERROR vs other log levels

2. **Time Trend Analysis**:
   - Line charts showing log activity over time
   - Identification of peak activity periods

3. **Clustering Analysis**:
   - Grouping similar logs using DBSCAN
   - Visualizing cluster sizes and characteristics

4. **Performance Analysis**:
   - Response time distributions and outliers
   - Correlation between log levels and performance metrics

## Architecture

LogAI combines:

1. **C++ Core**: High-performance log parsing, template extraction, and analytics
2. **Python Bindings**: Seamless integration with Python ecosystem
3. **AI Agent**: LLM-powered assistant for intelligent log analysis
4. **Visualization Engine**: Data visualization capabilities using matplotlib and seaborn

## Requirements

- Python 3.8+
- Docker (for building the C++ extension)
- OpenAI API key (for AI-powered analysis)

## License

This project is licensed under the MIT License - see the LICENSE file for details. 