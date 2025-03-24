#!/bin/bash
set -e  # Exit on any error

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m'  # No Color

echo -e "${BLUE}=== LogAI Agent Setup ===${NC}"
echo "This script will set up the LogAI Agent with all dependencies."

# Get the root directory of the project
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
PYTHON_DIR="${ROOT_DIR}/python"
VENV_DIR="${PYTHON_DIR}/venv"

# Create directories if they don't exist
mkdir -p "${BUILD_DIR}"
mkdir -p "${PYTHON_DIR}"

# Check dependencies
echo -e "\n${BLUE}Checking dependencies...${NC}"

# Check for CMake
if ! command -v cmake &> /dev/null; then
    echo -e "${RED}CMake is not installed. Please install CMake 3.14+ and try again.${NC}"
    exit 1
fi
echo -e "${GREEN}✓ CMake found$(cmake --version | head -n1 | cut -d" " -f3)${NC}"

# Check for Python
if ! command -v python3 &> /dev/null; then
    echo -e "${RED}Python 3 is not installed. Please install Python 3.8+ and try again.${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Python found$(python3 --version)${NC}"

# Check for pip
if ! command -v pip3 &> /dev/null; then
    echo -e "${RED}pip3 is not installed. Please install pip and try again.${NC}"
    exit 1
fi
echo -e "${GREEN}✓ pip found$(pip3 --version)${NC}"

# Check for C++ compiler
if ! command -v g++ &> /dev/null && ! command -v clang++ &> /dev/null; then
    echo -e "${RED}No C++ compiler found. Please install GCC 9+ or Clang 10+ and try again.${NC}"
    exit 1
fi

if command -v g++ &> /dev/null; then
    echo -e "${GREEN}✓ GCC found$(g++ --version | head -n1)${NC}"
else
    echo -e "${GREEN}✓ Clang found$(clang++ --version | head -n1)${NC}"
fi

# Check for required libraries
echo -e "\n${BLUE}Checking for required libraries...${NC}"
echo -e "${YELLOW}Note: Missing libraries will be installed via pip3${NC}"

# Build C++ components
echo -e "\n${BLUE}Building C++ components...${NC}"
cd "${BUILD_DIR}"

# Configure the build
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build . --config Release -j $(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo -e "${GREEN}✓ C++ components built successfully${NC}"

# Set up Python environment
echo -e "\n${BLUE}Setting up Python environment...${NC}"
cd "${ROOT_DIR}"

# Create virtual environment if it doesn't exist
if [ ! -d "${VENV_DIR}" ]; then
    echo "Creating virtual environment..."
    python3 -m venv "${VENV_DIR}"
fi

# Activate virtual environment
echo "Activating virtual environment..."
if [[ "$OSTYPE" == "darwin"* || "$OSTYPE" == "linux-gnu"* ]]; then
    source "${VENV_DIR}/bin/activate"
elif [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" || "$OSTYPE" == "win32" ]]; then
    source "${VENV_DIR}/Scripts/activate"
else
    echo -e "${RED}Unsupported OS. Please activate the virtual environment manually.${NC}"
    exit 1
fi

# Install Python package and dependencies
echo -e "\n${BLUE}Installing Python package and dependencies...${NC}"
cd "${PYTHON_DIR}"
pip install -e .

echo -e "\n${GREEN}✓ LogAI Agent has been successfully set up!${NC}"

# Check for OpenAI API key
if [ -z "$OPENAI_API_KEY" ]; then
    echo -e "${YELLOW}Warning: OPENAI_API_KEY environment variable not set.${NC}"
    echo -e "You will need to set your OpenAI API key before using the agent:"
    echo -e "    export OPENAI_API_KEY=your-api-key"
fi

# Show usage instructions
echo -e "\n${BLUE}Usage Instructions:${NC}"
echo -e "1. Activate the virtual environment if not already active:"
echo -e "   ${YELLOW}source ${VENV_DIR}/bin/activate${NC}"
echo -e "2. Run the LogAI agent:"
echo -e "   ${YELLOW}logai-agent --log-file path/to/your/logfile.log${NC}"
echo -e "3. Or specify a log format:"
echo -e "   ${YELLOW}logai-agent --log-file path/to/your/logfile.log --format jsonl${NC}"

# Make the script executable if not already
chmod +x "${ROOT_DIR}/setup_logai_agent.sh" 