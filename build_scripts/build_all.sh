#!/bin/bash

# Exit on error
set -e

# Define colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Clear previous builds
rm -rf bin lib output

# Create directories
mkdir -p bin lib output

echo -e "${GREEN}===========================================${NC}"
echo -e "${GREEN}      Building LogAI for all platforms     ${NC}"
echo -e "${GREEN}===========================================${NC}"

# Detect platform
if [[ "$(uname)" == "Darwin" ]]; then
    IS_MACOS=1
    if [[ "$(uname -m)" == "arm64" ]]; then
        IS_ARM64=1
    fi
else
    IS_MACOS=0
fi

# Check if Docker is available
if ! command -v docker &> /dev/null; then
    echo -e "${RED}Error: Docker is required for Linux builds but not installed${NC}"
    if [[ $IS_MACOS -eq 1 ]]; then
        echo -e "${YELLOW}Will only build for macOS${NC}"
    else
        echo -e "${YELLOW}Please install Docker: https://docs.docker.com/get-docker/${NC}"
        exit 1
    fi
    DOCKER_AVAILABLE=0
else
    DOCKER_AVAILABLE=1
fi

# Function to build for Linux ARM64
build_linux_arm64() {
    echo -e "${BLUE}Building for Linux ARM64...${NC}"
    
    # Build the Docker image
    docker build -f build_scripts/Dockerfile.linux-arm64 -t logai-linux-arm64 .
    
    # Extract the built files
    echo -e "${YELLOW}Extracting build artifacts...${NC}"
    docker create --name logai-extract-arm64 logai-linux-arm64
    docker cp logai-extract-arm64:/output/. ./output/
    docker rm logai-extract-arm64
    
    # Organize files
    mkdir -p bin/linux-arm64 lib/linux-arm64
    cp -r output/bin/linux-arm64/* bin/linux-arm64/
    cp -r output/lib/linux-arm64/* lib/linux-arm64/
    cp output/deploy.sh ./deploy_linux_arm64.sh
    
    echo -e "${GREEN}Linux ARM64 build completed successfully!${NC}"
}

# Function to build for Linux x86_64
build_linux_x86_64() {
    echo -e "${BLUE}Building for Linux x86_64...${NC}"
    
    # Build the Docker image
    docker build -f build_scripts/Dockerfile.linux-x86_64 -t logai-linux-x86_64 .
    
    # Extract the built files
    echo -e "${YELLOW}Extracting build artifacts...${NC}"
    docker create --name logai-extract-x86_64 logai-linux-x86_64
    docker cp logai-extract-x86_64:/output/. ./output/
    docker rm logai-extract-x86_64
    
    # Organize files
    mkdir -p bin/linux-x86_64 lib/linux-x86_64
    cp -r output/bin/linux-x86_64/* bin/linux-x86_64/
    cp -r output/lib/linux-x86_64/* lib/linux-x86_64/
    cp output/deploy-x86_64.sh ./deploy_linux_x86_64.sh
    
    echo -e "${GREEN}Linux x86_64 build completed successfully!${NC}"
}

# Function to build for macOS ARM64
build_macos_arm64() {
    echo -e "${BLUE}Building for macOS ARM64...${NC}"
    
    if [[ $IS_MACOS -eq 1 && $IS_ARM64 -eq 1 ]]; then
        # We're on the right platform, build natively
        bash build_scripts/build_macos_arm64.sh
    else
        echo -e "${RED}Error: Cannot build for macOS ARM64 on this platform${NC}"
        echo -e "${YELLOW}To build for macOS ARM64, run this script on an Apple Silicon Mac${NC}"
    fi
}

# Function to create a combined package
create_package() {
    echo -e "${BLUE}Creating combined package...${NC}"
    
    # Create output directory
    mkdir -p release
    
    # Copy binaries and libraries
    cp -r bin release/
    cp -r lib release/
    
    # Copy deployment scripts
    if [[ -f deploy_linux_arm64.sh ]]; then
        cp deploy_linux_arm64.sh release/
    fi
    if [[ -f deploy_linux_x86_64.sh ]]; then
        cp deploy_linux_x86_64.sh release/
    fi
    if [[ -f deploy_macos_arm64.sh ]]; then
        cp deploy_macos_arm64.sh release/
    fi
    
    # Create master deployment script
    cat > release/deploy.sh << 'EOF'
#!/bin/bash
# Detect platform
if [[ "$(uname)" == "Darwin" ]]; then
    if [[ "$(uname -m)" == "arm64" ]]; then
        echo "Detected macOS ARM64"
        ./deploy_macos_arm64.sh
    else
        echo "macOS x86_64 is not supported by this package"
    fi
else
    if [[ "$(uname -m)" == "aarch64" ]]; then
        echo "Detected Linux ARM64"
        ./deploy_linux_arm64.sh
    else
        echo "Detected Linux x86_64"
        ./deploy_linux_x86_64.sh
    fi
fi
EOF
    chmod +x release/deploy.sh
    
    # Create archive
    tar -czvf logai-release.tar.gz -C release .
    
    echo -e "${GREEN}Package created: ${NC}logai-release.tar.gz"
}

# Build for each platform
if [[ $DOCKER_AVAILABLE -eq 1 ]]; then
    build_linux_arm64
    build_linux_x86_64
fi

if [[ $IS_MACOS -eq 1 && $IS_ARM64 -eq 1 ]]; then
    build_macos_arm64
fi

# Create the final package
create_package

echo -e "${GREEN}===========================================${NC}"
echo -e "${GREEN}      Build process completed              ${NC}"
echo -e "${GREEN}===========================================${NC}"

# Output platform-specific information
echo -e "${YELLOW}Available binaries:${NC}"
if [[ -d bin/linux-arm64 ]]; then
    echo -e "  Linux ARM64: ${NC}bin/linux-arm64/logai"
fi
if [[ -d bin/linux-x86_64 ]]; then
    echo -e "  Linux x86_64: ${NC}bin/linux-x86_64/logai"
fi
if [[ -d bin/macos-arm64 ]]; then
    echo -e "  macOS ARM64: ${NC}bin/macos-arm64/logai"
fi

echo -e "${GREEN}Combined package: ${NC}logai-release.tar.gz"
echo -e "${YELLOW}To deploy, extract the archive and run ./deploy.sh${NC}" 