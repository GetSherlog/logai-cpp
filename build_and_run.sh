#!/bin/bash
# LogAI Docker Build and Run Script
#
# This script builds the LogAI C++ extension in Docker for different platforms
# and provides instructions for using the LogAI agent CLI.

set -e

# ANSI color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Detect platform
PLATFORM=$(uname -s)
ARCH=$(uname -m)

echo -e "${BLUE}LogAI Build and Run Script${NC}"
echo -e "${BLUE}=========================${NC}"
echo ""
echo -e "Detected platform: ${GREEN}$PLATFORM${NC} (${GREEN}$ARCH${NC})"
echo ""

# Check for Docker
if ! command -v docker &> /dev/null; then
    echo -e "${RED}Error: Docker is not installed or not in PATH.${NC}"
    echo "Please install Docker first: https://docs.docker.com/get-docker/"
    exit 1
fi

echo -e "${YELLOW}Select target platform:${NC}"
echo "1) Linux (x86_64)"
echo "2) macOS (x86_64)"
echo "3) macOS (arm64/Apple Silicon)"
echo "4) Windows (x86_64, via WSL)"
echo -e "${YELLOW}Enter selection [1-4]:${NC}"
read -r selection

# Set Docker arguments based on selection
case $selection in
    1)
        PLATFORM_NAME="linux-x86_64"
        BASE_IMAGE="ubuntu:22.04"
        BUILD_ARGS="--build-arg CXX=g++ --build-arg EXTRA_FLAGS=-static-libstdc++"
        ;;
    2)
        PLATFORM_NAME="macos-x86_64"
        BASE_IMAGE="ubuntu:22.04"
        BUILD_ARGS="--build-arg CXX=clang++ --build-arg EXTRA_FLAGS=-target x86_64-apple-darwin"
        ;;
    3)
        PLATFORM_NAME="macos-arm64"
        BASE_IMAGE="ubuntu:22.04"
        BUILD_ARGS="--build-arg CXX=clang++ --build-arg EXTRA_FLAGS=-target arm64-apple-darwin"
        ;;
    4)
        PLATFORM_NAME="windows-x86_64"
        BASE_IMAGE="ubuntu:22.04"
        BUILD_ARGS="--build-arg CXX=x86_64-w64-mingw32-g++ --build-arg EXTRA_FLAGS=-static"
        ;;
    *)
        echo -e "${RED}Invalid selection. Exiting.${NC}"
        exit 1
        ;;
esac

echo ""
echo -e "${BLUE}Building LogAI C++ extension for ${GREEN}$PLATFORM_NAME${NC}..."
echo ""

# Create Dockerfile
cat > Dockerfile.logai << EOF
FROM $BASE_IMAGE

# Install dependencies
RUN apt-get update && apt-get install -y \\
    build-essential \\
    cmake \\
    git \\
    python3 \\
    python3-dev \\
    python3-pip \\
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /logai-cpp

# Copy source files
COPY . .

# Build arguments
ARG CXX=g++
ARG EXTRA_FLAGS=""

# Build C++ extension
RUN pip3 install pybind11 numpy && \\
    mkdir -p build && \\
    cd build && \\
    CXX=\${CXX} cmake .. -DCMAKE_CXX_FLAGS="\${EXTRA_FLAGS}" && \\
    make -j \$(nproc)

# Copy built extension back to a volume
CMD cp -v build/logai_cpp*.so /output/ && echo "Build completed successfully!"
EOF

# Build Docker image
echo -e "${BLUE}Building Docker image...${NC}"
docker build $BUILD_ARGS -t logai-cpp-builder:$PLATFORM_NAME -f Dockerfile.logai .

# Create output directory if it doesn't exist
mkdir -p output

# Run Docker container to build the extension
echo -e "${BLUE}Running build container...${NC}"
docker run --rm -v "$(pwd)/output:/output" logai-cpp-builder:$PLATFORM_NAME

echo ""
echo -e "${GREEN}Build process completed!${NC}"
echo -e "Extension file should be in the ${YELLOW}output${NC} directory."
echo ""

# Install Python dependencies
echo -e "${BLUE}Installing Python dependencies...${NC}"
pip install -r requirements.txt

# Copy the built extension to the right location
echo -e "${BLUE}Copying built extension to module directory...${NC}"
mkdir -p python
cp output/logai_cpp*.so python/

echo ""
echo -e "${GREEN}Setup completed successfully!${NC}"
echo ""
echo -e "${BLUE}Using the LogAI Agent CLI${NC}"
echo -e "${BLUE}======================${NC}"
echo ""
echo -e "1. Set your OpenAI API key:"
echo -e "   ${YELLOW}export OPENAI_API_KEY=your_api_key_here${NC}"
echo ""
echo -e "2. Run the CLI with a log file:"
echo -e "   ${YELLOW}python python/logai_agent_cli.py --log-file path/to/your/logfile.log${NC}"
echo ""
echo -e "3. Use the interactive CLI to ask questions about your log data:"
echo -e "   - \"Show me all error logs from the last 24 hours\""
echo -e "   - \"Find patterns of failed connection attempts\""
echo -e "   - \"Visualize the distribution of log levels\""
echo -e "   - \"Cluster logs by response time and analyze outliers\""
echo ""
echo -e "4. Example visualization:"
echo -e "   ${YELLOW}python python/examples/visualization_example.py${NC}"
echo ""
echo -e "5. Example clustering:"
echo -e "   ${YELLOW}python python/examples/log_clustering.py${NC}"
echo ""
echo -e "${GREEN}Happy log analyzing!${NC}" 