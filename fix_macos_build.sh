#!/bin/bash
# Script to fix macOS C++ header issues by setting SDKROOT

# ANSI color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Check if we're on macOS
if [ "$(uname)" != "Darwin" ]; then
    echo -e "${RED}This script is only for macOS systems.${NC}"
    exit 1
fi

# Get the SDK path
SDK_PATH=$(xcrun --show-sdk-path)
echo -e "${GREEN}Found macOS SDK at: ${SDK_PATH}${NC}"

# Export the SDKROOT variable for current session
export SDKROOT=$SDK_PATH
echo -e "${GREEN}Set SDKROOT=${SDKROOT} for current session${NC}"

# Determine user's shell
USER_SHELL=$(basename "$SHELL")
echo -e "${BLUE}Detected shell: ${USER_SHELL}${NC}"

# Path to shell config file
CONFIG_FILE=""
case "$USER_SHELL" in
    "bash")
        CONFIG_FILE="$HOME/.bash_profile"
        if [ ! -f "$CONFIG_FILE" ]; then
            CONFIG_FILE="$HOME/.bashrc"
        fi
        ;;
    "zsh")
        CONFIG_FILE="$HOME/.zshrc"
        ;;
    *)
        echo -e "${YELLOW}Unsupported shell. Please add 'export SDKROOT=$(xcrun --show-sdk-path)' to your shell configuration file manually.${NC}"
        ;;
esac

# Add to shell config if possible
if [ -n "$CONFIG_FILE" ]; then
    if grep -q "export SDKROOT=" "$CONFIG_FILE"; then
        echo -e "${YELLOW}SDKROOT already configured in ${CONFIG_FILE}${NC}"
    else
        echo -e "\n# Set SDKROOT for C++ development" >> "$CONFIG_FILE"
        echo "export SDKROOT=\$(xcrun --show-sdk-path)" >> "$CONFIG_FILE"
        echo -e "${GREEN}Added SDKROOT to ${CONFIG_FILE}${NC}"
        echo -e "${BLUE}Please restart your terminal or run 'source ${CONFIG_FILE}' to apply the changes.${NC}"
    fi
fi

# Clean and rebuild
echo -e "${BLUE}Do you want to clean and rebuild the project now? (y/n)${NC}"
read -r rebuild_choice

if [ "$rebuild_choice" = "y" ] || [ "$rebuild_choice" = "Y" ]; then
    echo -e "${BLUE}Cleaning build directory...${NC}"
    rm -rf build
    mkdir -p build
    
    echo -e "${BLUE}Configuring with CMake...${NC}"
    cd build
    cmake ..
    
    echo -e "${BLUE}Building project...${NC}"
    make -j$(sysctl -n hw.ncpu)
    
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}Build completed successfully!${NC}"
    else
        echo -e "${RED}Build failed.${NC}"
    fi
else
    echo -e "${BLUE}To rebuild, run the build_and_run.sh script and select option 3.${NC}"
fi

echo -e "\n${GREEN}SDKROOT fix has been applied. Future terminal sessions will have the correct environment variable.${NC}" 