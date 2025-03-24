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

# Check if we're on Apple Silicon Mac
IS_APPLE_SILICON=false
if [ "$PLATFORM" = "Darwin" ] && [ "$ARCH" = "arm64" ]; then
    IS_APPLE_SILICON=true
    echo -e "${GREEN}Detected Apple Silicon Mac!${NC}"
fi

# Check for Docker (only if not doing native Apple Silicon build)
if ! $IS_APPLE_SILICON && ! command -v docker &> /dev/null; then
    echo -e "${RED}Error: Docker is not installed or not in PATH.${NC}"
    echo "Please install Docker first: https://docs.docker.com/get-docker/"
    exit 1
fi

# If on Apple Silicon, ask if user wants native build
USE_NATIVE_BUILD=false
if $IS_APPLE_SILICON; then
    echo -e "${YELLOW}Would you like to build natively on this Apple Silicon Mac?${NC}"
    echo "1) Yes, build natively (recommended for M1/M2/M3 Macs)"
    echo "2) No, use Docker cross-compilation"
    echo -e "${YELLOW}Enter selection [1-2]:${NC}"
    read -r native_selection
    
    if [ "$native_selection" = "1" ]; then
        USE_NATIVE_BUILD=true
    fi
fi

if $USE_NATIVE_BUILD; then
    echo -e "${BLUE}Setting up native build for Apple Silicon Mac...${NC}"
    
    # Check for necessary tools
    if ! command -v brew &> /dev/null; then
        echo -e "${YELLOW}Homebrew not detected. Please install it to continue:${NC}"
        echo "  /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
        exit 1
    fi
    
    # Install dependencies
    echo -e "${BLUE}Installing build dependencies...${NC}"
    brew install cmake pybind11 duckdb spdlog apache-arrow lz4
    
    # Make sure arrow is visible to CMake
    export ARROW_HOME="$(brew --prefix apache-arrow)"
    
    # Install DuckDB C++ library
    echo -e "${BLUE}Installing DuckDB C++ development headers...${NC}"
    DUCKDB_DIR="$(pwd)/duckdb-lib"
    mkdir -p "$DUCKDB_DIR"
    curl -L -o "$DUCKDB_DIR/duckdb.zip" "https://github.com/duckdb/duckdb/releases/latest/download/libduckdb-osx-universal.zip"
    unzip -o "$DUCKDB_DIR/duckdb.zip" -d "$DUCKDB_DIR"
    
    # Build the extension natively
    echo -e "${BLUE}Building the C++ extension natively...${NC}"
    mkdir -p build
    cd build
    cmake .. -DCMAKE_PREFIX_PATH="$DUCKDB_DIR" \
             -DENABLE_LLAMA_CPP=OFF
    make -j $(sysctl -n hw.ncpu)
    cd ..
    
    # Create output and python directories
    mkdir -p output python
    cp build/logai_cpp*.so output/
    cp build/logai_cpp*.so python/
    
else
    # Docker build path
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
    CXX=\${CXX} cmake .. -DCMAKE_CXX_FLAGS="\${EXTRA_FLAGS}" -DENABLE_LLAMA_CPP=OFF && \\
    make -j \$(nproc)

# Copy built extension back to a volume
CMD cp -v build/logai_cpp*.so /output/ && echo "Build completed successfully!"
EOF

    # Build Docker image
    echo -e "${BLUE}Building Docker image...${NC}"

    # Use explicit path argument and separate command components
    CONTEXT_PATH=$(pwd)
    echo "Using build context path: $CONTEXT_PATH"
    echo "Using Dockerfile: $CONTEXT_PATH/Dockerfile.logai"

    # Build command with environment variables to disable BuildKit
    BUILD_CMD="docker"
    BUILD_CMD="$BUILD_CMD build"
    BUILD_CMD="$BUILD_CMD -f $CONTEXT_PATH/Dockerfile.logai"
    BUILD_CMD="$BUILD_CMD $BUILD_ARGS"
    BUILD_CMD="$BUILD_CMD -t logai-cpp-builder:$PLATFORM_NAME"
    BUILD_CMD="$BUILD_CMD $CONTEXT_PATH"

    # Display the full command for debugging
    echo -e "${YELLOW}Executing: $BUILD_CMD${NC}"

    # Execute the build command
    eval "$BUILD_CMD"

    # Create output directory if it doesn't exist
    mkdir -p output

    # Run Docker container to build the extension
    echo -e "${BLUE}Running build container...${NC}"
    docker run --rm -v "$(pwd)/output:/output" logai-cpp-builder:$PLATFORM_NAME
fi

echo ""
echo -e "${GREEN}Build process completed!${NC}"
echo -e "Extension file should be in the ${YELLOW}output${NC} directory."
echo ""

# Install Python dependencies
echo -e "${BLUE}Installing Python dependencies...${NC}"
pip install -r requirements.txt

# Copy the built extension to the right location (if not already done for native build)
if ! $USE_NATIVE_BUILD; then
    echo -e "${BLUE}Copying built extension to module directory...${NC}"
    mkdir -p python
    cp output/logai_cpp*.so python/
fi

echo ""
echo -e "${GREEN}Setup completed successfully!${NC}"
echo ""
echo -e "${BLUE}Using the LogAI Agent CLI${NC}"
echo -e "${BLUE}======================${NC}"
echo ""
echo -e "1. Set your LLM provider API key (OpenAI is the default):"
echo -e "   ${YELLOW}export OPENAI_API_KEY=your_api_key_here${NC}"
echo ""
echo -e "   You can also use other LLM providers:"
echo -e "   For Ollama: ${YELLOW}export LLM_PROVIDER=ollama${NC}"
echo -e "              ${YELLOW}export OLLAMA_ENDPOINT=http://localhost:11434/api/generate${NC}"
echo -e "              ${YELLOW}export OLLAMA_MODEL=llama3${NC}"
echo ""
echo -e "   For Google Gemini: ${YELLOW}export LLM_PROVIDER=gemini${NC}"
echo -e "                     ${YELLOW}export GEMINI_API_KEY=your_api_key_here${NC}"
echo -e "                     ${YELLOW}export GEMINI_MODEL=gemini-pro${NC}"
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