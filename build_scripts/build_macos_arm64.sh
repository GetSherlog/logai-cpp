#!/bin/bash

# Exit on error
set -e

# Define colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Building LogAI for macOS ARM64${NC}"

# Check if running on macOS ARM64
if [[ "$(uname)" != "Darwin" || "$(uname -m)" != "arm64" ]]; then
    echo -e "${RED}Error: This script must be run on macOS ARM64 (Apple Silicon)${NC}"
    exit 1
fi

# Check for Homebrew
if ! command -v brew &> /dev/null; then
    echo -e "${RED}Error: Homebrew is required but not installed${NC}"
    echo -e "${YELLOW}Please install Homebrew: https://brew.sh${NC}"
    exit 1
fi

# Install dependencies
echo -e "${YELLOW}Installing dependencies...${NC}"
brew install \
    cmake \
    eigen \
    abseil \
    nlohmann-json \
    apache-arrow \
    curl \
    folly \
    duckdb \
    cxxopts \
    pkg-config \
    spdlog \
    llama.cpp

# Create build directory
echo -e "${YELLOW}Creating build directory...${NC}"
mkdir -p build
cd build

# Configure and build
echo -e "${YELLOW}Configuring and building...${NC}"
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(sysctl -n hw.ncpu)

# Create output directory structure
echo -e "${YELLOW}Creating output package...${NC}"
mkdir -p ../bin/macos-arm64
cp bin/macos-arm64/logai ../bin/macos-arm64/

# Create lib directory for dependencies
mkdir -p ../lib/macos-arm64

# Copy required dynamic libraries
echo -e "${YELLOW}Copying dependencies...${NC}"
# Use otool to find dependencies
DEPS=$(otool -L ../bin/macos-arm64/logai | grep -v '/System/Library' | grep -v '/usr/lib' | grep -v '@rpath' | awk '{print $1}')

for lib in $DEPS; do
    echo "Copying $lib"
    cp "$lib" ../lib/macos-arm64/
done

# Create a deploy script
echo -e "${YELLOW}Creating deployment script...${NC}"
cat > ../deploy_macos_arm64.sh << 'EOF'
#!/bin/bash
mkdir -p ~/.local/lib
cp ./lib/macos-arm64/* ~/.local/lib/
echo "Libraries copied to ~/.local/lib/"
chmod +x ./bin/macos-arm64/logai
echo "You can now run the binary with: ./bin/macos-arm64/logai"
EOF

chmod +x ../deploy_macos_arm64.sh

echo -e "${GREEN}Build completed successfully!${NC}"
echo -e "${YELLOW}Binary is available at: ${NC}$(pwd)/../bin/macos-arm64/logai"
echo -e "${YELLOW}Run deployment script with: ${NC}./deploy_macos_arm64.sh" 