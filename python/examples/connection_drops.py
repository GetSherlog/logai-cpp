#!/usr/bin/env python3
"""
Example script that demonstrates using the LogAI agent to analyze connection drops
"""

import os
import sys
import time

# Add the parent directory to the path to import the LogAI agent
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from logai_agent import LogAIAgent

def main():
    """Main function to demonstrate the LogAI agent."""
    # Check for OpenAI API key
    if not os.environ.get("OPENAI_API_KEY"):
        print("Error: OPENAI_API_KEY environment variable not set.")
        print("Please set your OpenAI API key with:")
        print("    export OPENAI_API_KEY=your-api-key")
        return 1
    
    # Create the agent
    print("Initializing LogAI Agent...")
    agent = LogAIAgent()
    
    # Sample log file
    sample_log_file = os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
        "samples/sample_logs.jsonl"
    )
    
    # Initialize the agent with the sample log file
    print(f"Loading log file: {sample_log_file}")
    if not agent.initialize(sample_log_file, "jsonl"):
        print("Failed to initialize the agent.")
        return 1
    
    print("\nAgent initialized and ready to answer questions!")
    print("Example query: How many times did connection drop for users in the last hour?")
    print("Processing query...")
    
    # Process the example query
    start_time = time.time()
    response = agent.process_query("How many times did connection drop for users in the last hour?")
    end_time = time.time()
    
    # Print the response
    print("\n----- Response -----")
    print(response)
    print("--------------------")
    print(f"Response time: {end_time - start_time:.2f} seconds")
    
    # Now let's try another query
    print("\nTrying another query: Which user had the most connection drops and why?")
    print("Processing query...")
    
    # Process the second query
    start_time = time.time()
    response = agent.process_query("Which user had the most connection drops and why?")
    end_time = time.time()
    
    # Print the response
    print("\n----- Response -----")
    print(response)
    print("--------------------")
    print(f"Response time: {end_time - start_time:.2f} seconds")
    
    return 0

if __name__ == "__main__":
    sys.exit(main()) 