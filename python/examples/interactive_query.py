#!/usr/bin/env python3
"""
Example script that demonstrates using the LogAI agent in interactive mode
"""

import os
import sys

# Add the parent directory to the path to import the LogAI agent
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from logai_agent import LogAIAgent

def main():
    """Main function to demonstrate the LogAI agent in interactive mode."""
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
    
    print("\nAgent initialized and ready for interactive queries!")
    print("Type 'exit' or 'quit' to end the session.")
    print("\nSome example questions you can ask:")
    print("- How many errors occurred in the last 2 hours?")
    print("- When did the first connection drop happen?")
    print("- Which user experienced the most connection drops?")
    print("- What was happening during periods of high CPU usage?")
    print("- Summarize all disk I/O related issues")
    
    # Run the interactive CLI
    agent.run_interactive_cli()
    
    return 0

if __name__ == "__main__":
    sys.exit(main()) 