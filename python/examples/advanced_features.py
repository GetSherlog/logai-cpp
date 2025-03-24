#!/usr/bin/env python3
"""
Advanced LogAI Features Example

This script demonstrates the advanced features of the LogAI library,
including time filtering, log level filtering, statistics, trending patterns,
field filtering, anomaly detection, and output formatting.
"""

import os
import sys
import json
from pathlib import Path

# Add the parent directory to the Python path
sys.path.append(str(Path(__file__).parent.parent))

# Import our LogAI wrapper
from logai_cpp_wrapper import LogAICppWrapper

def print_section(title):
    """Print a section title."""
    print("\n" + "=" * 50)
    print(f" {title}")
    print("=" * 50)

def display_result(result):
    """Pretty print JSON results."""
    print(json.dumps(result, indent=2))
    print()

def main():
    # Initialize the LogAI wrapper
    logai = LogAICppWrapper()
    
    # Load sample logs
    sample_path = os.path.join(Path(__file__).parent.parent, "samples", "sample_logs.jsonl")
    logai.load_logs(sample_path)
    print(f"Loaded logs from {sample_path}")
    
    # 1. Filter by Time Range
    print_section("FILTERING LOGS BY TIME RANGE")
    time_filtered = logai.filter_by_time(since="2023-09-01T00:00:00", until="2023-09-01T12:00:00")
    print(f"Found {len(time_filtered.get('logs', []))} logs in the time range")
    display_result(time_filtered)
    
    # 2. Filter by Log Level
    print_section("FILTERING LOGS BY LOG LEVEL")
    level_filtered = logai.filter_by_level(levels=["ERROR", "CRITICAL"], exclude_levels=["DEBUG"])
    print(f"Found {len(level_filtered.get('logs', []))} logs with specified levels")
    display_result(level_filtered)
    
    # 3. Calculate Statistics
    print_section("LOG STATISTICS")
    statistics = logai.calculate_statistics()
    display_result(statistics)
    
    # 4. Get Trending Patterns
    print_section("TRENDING PATTERNS")
    trends = logai.get_trending_patterns(time_window="hour")
    display_result(trends)
    
    # 5. Filter by Fields
    print_section("FILTERING BY FIELDS")
    field_filtered = logai.filter_by_fields(
        include_fields=["timestamp", "level", "message"],
        exclude_fields=["thread_id"],
        field_pattern="connection"
    )
    print(f"Found {len(field_filtered.get('logs', []))} logs with matching fields")
    display_result(field_filtered)
    
    # 6. Detect Anomalies
    print_section("ANOMALY DETECTION")
    anomalies = logai.detect_anomalies(threshold=2.0)
    display_result(anomalies)
    
    # 7. Format Logs
    print_section("LOG FORMATTING")
    # Get a few logs to format
    some_logs = logai.search_logs("error", limit=3).get('logs', [])
    
    formats = ["json", "text", "csv", "logfmt"]
    for fmt in formats:
        print(f"\nFormat: {fmt.upper()}")
        formatted = logai.format_logs(format=fmt, logs_json=some_logs)
        print(formatted.get('formatted_output', ''))

if __name__ == "__main__":
    main() 