#!/usr/bin/env python3
"""
LogAI Clustering Example

This script demonstrates how to use DBSCAN clustering to identify
groups of similar log entries based on various features.
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

def main():
    # Initialize the LogAI wrapper
    logai = LogAICppWrapper()
    
    # Load sample logs
    sample_path = os.path.join(Path(__file__).parent.parent, "samples", "sample_logs.jsonl")
    logai.load_logs(sample_path)
    print(f"Loaded logs from {sample_path}")
    
    # 1. Basic clustering by log level
    print_section("BASIC CLUSTERING BY LOG LEVEL")
    level_clusters = logai.cluster_logs(
        eps=0.1,              # Small epsilon means exact matches
        min_samples=2,        # At least 2 logs to form a cluster
        features=["level"]    # Cluster by log level
    )
    
    print(f"Found {level_clusters.get('cluster_count', 0)} clusters")
    
    # Print basic info about each cluster
    for cluster in level_clusters.get('clusters', []):
        if cluster['id'] == -1:
            print(f"Noise points: {cluster['size']} logs")
        else:
            # Get the level of the first log in the cluster
            if cluster['logs']:
                level = cluster['logs'][0].get('level', 'UNKNOWN')
                print(f"Cluster {cluster['id']}: {cluster['size']} logs with level {level}")
    
    # 2. Clustering by response time and error status
    print_section("CLUSTERING BY RESPONSE TIME AND ERROR STATUS")
    performance_clusters = logai.cluster_logs(
        eps=0.5,
        min_samples=3,
        features=["response_time", "status_code"]
    )
    
    print(f"Found {performance_clusters.get('cluster_count', 0)} performance clusters")
    
    # Print detailed info about each cluster
    for cluster in performance_clusters.get('clusters', []):
        if cluster['id'] == -1:
            print(f"Noise points: {cluster['size']} logs that don't fit any cluster")
            continue
            
        # Get statistics for this cluster
        response_times = []
        status_codes = []
        
        for log in cluster['logs']:
            if 'response_time' in log:
                try:
                    response_times.append(float(log['response_time']))
                except (ValueError, TypeError):
                    pass
                    
            if 'status_code' in log:
                try:
                    status_codes.append(int(log['status_code']))
                except (ValueError, TypeError):
                    pass
        
        # Calculate average response time
        avg_response_time = sum(response_times) / len(response_times) if response_times else 0
        
        # Find most common status code
        status_count = {}
        for code in status_codes:
            status_count[code] = status_count.get(code, 0) + 1
            
        most_common_status = max(status_count.items(), key=lambda x: x[1])[0] if status_count else "N/A"
        
        print(f"Cluster {cluster['id']}: {cluster['size']} logs")
        print(f"  - Avg response time: {avg_response_time:.2f}ms")
        print(f"  - Most common status code: {most_common_status}")
        
        # Print a sample log from this cluster
        if cluster['logs']:
            sample_log = cluster['logs'][0]
            print(f"  - Sample log: {sample_log.get('message', 'N/A')}")
        
        print()
    
    # 3. Custom feature clustering
    print_section("CUSTOM FEATURE CLUSTERING")
    print("You can cluster logs using any numeric field in your logs.")
    print("Examples of useful features:")
    print("  - response_time: Group logs by performance")
    print("  - bytes_sent: Group logs by payload size")
    print("  - status_code: Group logs by HTTP status")
    print("  - user_id: Group logs by user activity")
    print("  - timestamp (converted to epoch): Group logs by time proximity")
    
    # Show a sample command
    print("\nSample command:")
    print("logai.cluster_logs(eps=0.5, min_samples=3, features=['response_time', 'bytes_sent'])")

if __name__ == "__main__":
    main() 