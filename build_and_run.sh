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

# Options menu
echo -e "${YELLOW}Select an option:${NC}"
echo "1) Build with Docker (standard build)"
echo "2) Build natively (macOS recommended)"
echo "3) Clean build (fixes build errors)"
echo -e "${YELLOW}Enter selection [1-3]:${NC}"
read -r build_selection

# Clean build option
if [ "$build_selection" = "3" ]; then
    echo -e "${BLUE}Performing clean build to fix errors...${NC}"
    
    echo -e "${BLUE}===== Cleaning build directory =====${NC}"
    rm -rf build
    mkdir -p build
    
    # Set SDKROOT environment variable to fix header search paths
    if [ "$PLATFORM" = "Darwin" ]; then
        export SDKROOT=$(xcrun --show-sdk-path)
        echo -e "${GREEN}Setting SDKROOT=${SDKROOT}${NC}"
    fi
    
    # Find SDK paths for macOS
    if [ "$PLATFORM" = "Darwin" ]; then
        echo -e "${BLUE}Detecting macOS SDK paths...${NC}"
        
        # Find the Xcode developer directory
        XCODE_PATH=$(xcode-select -p)
        echo "Xcode path: $XCODE_PATH"
        
        # Find the latest SDK path
        SDK_PATH=$(find "$XCODE_PATH/Platforms/MacOSX.platform/Developer/SDKs" -maxdepth 1 -name "MacOSX*.sdk" | sort -V | tail -1)
        if [ -z "$SDK_PATH" ]; then
            # Try the default path if not found
            SDK_PATH="$XCODE_PATH/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"
        fi
        
        # Check if SDK path exists
        if [ ! -d "$SDK_PATH" ]; then
            echo -e "${RED}Could not find macOS SDK at $SDK_PATH${NC}"
            SDK_PATH="/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"
            echo -e "${YELLOW}Trying default SDK path: $SDK_PATH${NC}"
        fi
        
        echo "Using SDK path: $SDK_PATH"
        
        # Find the C++ standard library headers
        CPP_INCLUDE_PATH=$(find "$XCODE_PATH/Toolchains" -path "*/usr/include/c++/v1" | head -1)
        if [ -z "$CPP_INCLUDE_PATH" ]; then
            # Try the default path if not found
            CPP_INCLUDE_PATH="/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/include/c++/v1"
        fi
        
        echo "Using C++ include path: $CPP_INCLUDE_PATH"
        
        # Set up compiler and build flags
        MACOS_FLAGS="-stdlib=libc++ -isystem $CPP_INCLUDE_PATH -isysroot $SDK_PATH"
        
        echo -e "${GREEN}Using macOS build flags: $MACOS_FLAGS${NC}"
    fi
    
    echo -e "${BLUE}===== Configuring CMake =====${NC}"
    cd build
    if [ "$PLATFORM" = "Darwin" ]; then
        # Use macOS-specific flags
        cmake .. -DCMAKE_CXX_FLAGS="$MACOS_FLAGS"
    else
        cmake ..
    fi
    
    echo -e "${BLUE}===== Building project =====${NC}"
    make -j$([ "$PLATFORM" = "Darwin" ] && sysctl -n hw.ncpu || nproc) VERBOSE=1
    
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}Clean build completed successfully!${NC}"
        cd ..
        
        # Set platform name if not set previously
        if [ -z "$PLATFORM_NAME" ]; then
            if [ "$PLATFORM" = "Darwin" ]; then
                PLATFORM_NAME="macos"
            else
                PLATFORM_NAME="linux"
            fi
        fi
        
        echo -e "${BLUE}The executable is located at:${NC} $(pwd)/build/bin/${PLATFORM_NAME}-${ARCH}/logai"
        
        # Install Python dependencies
        echo -e "${BLUE}Installing Python dependencies...${NC}"
        pip install -r requirements.txt
        
        echo ""
        echo -e "${GREEN}Setup completed successfully!${NC}"
    else
        echo -e "${RED}Build failed! Please check the error messages above.${NC}"
        exit 1
    fi
    
    # Skip the rest of the script
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
    echo -e "${GREEN}Happy log analyzing!${NC}"
    exit 0
fi

# Check for Docker (only if not doing native Apple Silicon build)
if [ "$build_selection" = "1" ] && ! command -v docker &> /dev/null; then
    echo -e "${RED}Error: Docker is not installed or not in PATH.${NC}"
    echo "Please install Docker first: https://docs.docker.com/get-docker/"
    exit 1
fi

# If user selected native build or is on Apple Silicon and selected option 2
USE_NATIVE_BUILD=false
if [ "$build_selection" = "2" ] || ([ "$build_selection" = "2" ] && $IS_APPLE_SILICON); then
    USE_NATIVE_BUILD=true
fi

if $USE_NATIVE_BUILD; then
    echo -e "${BLUE}Setting up native build for MacOS...${NC}"
    
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
    
    # Set SDKROOT environment variable for macOS
    if [ "$PLATFORM" = "Darwin" ]; then
        export SDKROOT=$(xcrun --show-sdk-path)
        echo -e "${GREEN}Setting SDKROOT=${SDKROOT} for native build${NC}"
    fi
    
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
    # Docker build path - only if option 1 was selected
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