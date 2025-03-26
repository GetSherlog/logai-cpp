#!/bin/bash
# LogAI Build Script
#
# This script automates the build process for LogAI C++ and Python CLI
# and produces a wheel package that can be installed on the host machine.

set -e

# ANSI color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}LogAI Build Process${NC}"
echo -e "${BLUE}===================${NC}"
echo ""

# Check for Docker
if ! command -v docker &> /dev/null; then
    echo -e "${RED}Error: Docker is not installed or not in PATH.${NC}"
    echo "Please install Docker first: https://docs.docker.com/get-docker/"
    exit 1
fi

# Clean old build artifacts
echo -e "${BLUE}[0/4] Cleaning old build artifacts...${NC}"
rm -rf build src/build python/build python/dist python/*.egg-info

# Set Docker image and container names
IMAGE_NAME="logai-cpp"
CONTAINER_NAME="logai-cpp-container"

# Step 1: Build the Docker image
echo -e "${BLUE}[1/4] Building Docker image...${NC}"
docker build -t $IMAGE_NAME -f Dockerfile .

if [ $? -ne 0 ]; then
    echo -e "${RED}Docker build failed!${NC}"
    exit 1
fi
echo -e "${GREEN}Docker image built successfully!${NC}"

# Step 2: Create directories
echo -e "${BLUE}[2/4] Creating necessary directories...${NC}"
mkdir -p logs uploads wheels

# Step 3: Run a temporary container to extract the wheel package
echo -e "${BLUE}[3/4] Extracting wheel package...${NC}"

# Stop and remove existing container if it exists
if docker ps -a | grep -q $CONTAINER_NAME; then
    echo -e "${YELLOW}Removing existing container...${NC}"
    docker stop $CONTAINER_NAME >/dev/null 2>&1 || true
    docker rm $CONTAINER_NAME >/dev/null 2>&1 || true
fi

# Create a temporary container
docker run -d --name $CONTAINER_NAME $IMAGE_NAME sleep 30

# Copy wheel package from container to host
echo -e "${BLUE}Copying wheel package to ./wheels directory...${NC}"
docker cp $CONTAINER_NAME:/shared/wheels/. ./wheels/

if [ $? -ne 0 ]; then
    echo -e "${RED}Failed to copy wheel package from container.${NC}"
    docker rm -f $CONTAINER_NAME >/dev/null 2>&1
    exit 1
fi

# Clean up temporary container
docker rm -f $CONTAINER_NAME >/dev/null 2>&1

# Step 4: Provide instructions
echo -e "${BLUE}[4/4] Setup complete!${NC}"

WHEEL_FILE=$(ls ./wheels/*.whl 2>/dev/null | head -1)
if [ -n "$WHEEL_FILE" ]; then
    echo -e "${GREEN}Successfully built wheel package:${NC}"
    echo -e "  ${YELLOW}$WHEEL_FILE${NC}"
    echo ""
    echo -e "${BLUE}To use LogAI CLI on your host machine:${NC}"
    echo -e "${YELLOW}# Install the wheel package"
    echo -e "pip install $WHEEL_FILE"
    echo -e ""
    echo -e "# Configure your LLM provider:"
    echo -e ""
    echo -e "# For OpenAI (default):"
    echo -e "export LLM_PROVIDER=openai"
    echo -e "export OPENAI_API_KEY=your_api_key_here"
    echo -e "export OPENAI_MODEL=gpt-4o  # Optional"
    echo -e ""
    echo -e "# For Google Gemini:"
    echo -e "export LLM_PROVIDER=gemini"
    echo -e "export GEMINI_API_KEY=your_api_key_here"
    echo -e "export GEMINI_MODEL=gemini-pro  # Optional"
    echo -e ""
    echo -e "# For Ollama (local LLMs):"
    echo -e "export LLM_PROVIDER=ollama"
    echo -e "export OLLAMA_ENDPOINT=http://localhost:11434/api/generate"
    echo -e "export OLLAMA_MODEL=llama3  # Specify your model"
    echo -e ""
    echo -e "# Run the CLI tool"
    echo -e "logai-agent --log-file path/to/your/logfile.log${NC}"
    echo ""
    echo -e "${GREEN}Run the Docker container for development (optional):${NC}"
    echo -e "${YELLOW}docker run -it --name $CONTAINER_NAME \\"
    echo -e "  -v \"\$(pwd):/workspace\" \\"
    echo -e "  -v \"\$(pwd)/logs:/workspace/logs\" \\"
    echo -e "  -v \"\$(pwd)/uploads:/workspace/uploads\" \\"
    echo -e "  -e LLM_PROVIDER=\"\${LLM_PROVIDER}\" \\"
    echo -e "  -e OPENAI_API_KEY=\"\${OPENAI_API_KEY}\" \\"
    echo -e "  -e GEMINI_API_KEY=\"\${GEMINI_API_KEY}\" \\"
    echo -e "  -e OLLAMA_ENDPOINT=\"\${OLLAMA_ENDPOINT}\" \\"
    echo -e "  -e OLLAMA_MODEL=\"\${OLLAMA_MODEL}\" \\"
    echo -e "  $IMAGE_NAME${NC}"
else
    echo -e "${RED}No wheel file found. Build process may have failed.${NC}"
    exit 1
fi

exit 0 