# LogAI Python Examples

This directory contains example scripts demonstrating how to use the LogAI Python wrapper.

## Available Examples

### Basic Examples

1. `connection_drops.py` - Analyzes network connection failures and dropouts in logs
2. `performance_patterns.py` - Identifies performance issues and patterns in logs
3. `interactive_query.py` - Runs an interactive CLI for querying and analyzing logs

### Advanced Features

4. `advanced_features.py` - Demonstrates the full range of LogAI capabilities:
   - Time-based filtering
   - Log level filtering
   - Statistical analysis
   - Trend detection
   - Field-based filtering
   - Anomaly detection
   - Log formatting options
   
5. `log_clustering.py` - Shows how to use DBSCAN clustering algorithm:
   - Group logs by log level
   - Identify performance clusters based on response time and status code
   - Create custom clusters using any numeric log field

6. `visualization_example.py` - Demonstrates data visualization capabilities:
   - Create bar charts of log level distributions
   - Generate line charts for trend analysis
   - Visualize clustering results with pie charts
   - Build custom visualizations with Python code

## Running the Examples

Make sure you've built the C++ extension first:

```bash
cd ../..
make clean
make
```

Then run any example script:

```bash
python3 python/examples/connection_drops.py
python3 python/examples/performance_patterns.py
python3 python/examples/interactive_query.py
python3 python/examples/advanced_features.py
python3 python/examples/log_clustering.py
python3 python/examples/visualization_example.py
```

## Using the LogAI Agent

For AI-powered log analysis, set your OpenAI API key and run:

```bash
export OPENAI_API_KEY=your_key_here
python3 python/logai_agent_cli.py
```

## Available Features

The LogAI library provides the following capabilities:

### Core Features
- Log loading and parsing
- Full-text and pattern-based search
- Template analysis
- Time range extraction
- Pattern occurrence counting
- Log summarization

### Advanced Features
- **Time Filtering**: Filter logs by specific time ranges
- **Log Level Filtering**: Filter by log levels (ERROR, WARN, INFO, etc.)
- **Statistics Calculation**: Get comprehensive statistics about your logs
- **Trend Detection**: Identify trending patterns over time
- **Field Filtering**: Filter based on specific fields and patterns
- **Anomaly Detection**: Find unusual patterns and outliers
- **Log Formatting**: Convert logs between different formats (JSON, CSV, text, logfmt)
- **Log Clustering**: Group similar logs using DBSCAN clustering algorithm
- **Data Visualization**: Create charts and graphs from log analysis results

## Sample Data

Sample log data is provided in the `../samples/` directory. You can use your own log files by specifying the path when loading logs.

## Prerequisites

Before running the examples, make sure you have:

1. Installed the LogAI package (follow the installation instructions in the main README)
2. Set up your OpenAI API key:
   ```bash
   export OPENAI_API_KEY=your-api-key
   ```

## Creating Your Own Examples

To create your own example:

1. Create a new Python file in this directory
2. Import the LogAI agent:
   ```python
   import os
   import sys
   sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
   from logai_agent import LogAIAgent
   ```

3. Initialize the agent and load your log file:
   ```python
   agent = LogAIAgent()
   agent.initialize("path/to/your/logfile.log", "format")
   ```

4. Process queries:
   ```python
   response = agent.process_query("Your natural language question here")
   print(response)
   ```

## Sample Log Files

Sample log files are provided in the `samples` directory. These include:

- `sample_logs.jsonl`: Sample logs in JSONL format
- `apache_logs.log`: Sample Apache web server logs
- `application_logs.log`: Sample application logs with various error patterns

Feel free to use these files for testing or create your own log files. 