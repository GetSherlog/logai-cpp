# LogAI - AI-Powered Log Analysis Tool

A powerful command-line tool for analyzing logs using AI, semantic search, and local LLM inference.

## Features

- Semantic search across log files with template extraction
- Pattern analysis with AI-powered insights
- Anomaly detection using machine learning algorithms
- DuckDB integration for structured data queries
- Vector embeddings for efficient similarity search
- Local LLM inference with llama.cpp integration
- High-performance processing with Folly data structures

## Dependencies

The following dependencies are required:

- C++17 or later
- CMake 3.15 or later
- cxxopts (for CLI argument parsing)
- Eigen3 (for linear algebra operations)
- abseil-cpp (for string utilities)
- nlohmann-json (for JSON processing)
- Apache Arrow (for efficient data handling)
- libcurl (for HTTP requests)
- Folly (for high-performance data structures)
- DuckDB (for structured data queries)
- llama.cpp (for local LLM inference)

### Installing Dependencies

On macOS (using Homebrew):
```bash
brew install cmake eigen abseil nlohmann-json apache-arrow curl folly duckdb
```

On Ubuntu/Debian:
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    libeigen3-dev \
    libabsl-dev \
    nlohmann-json3-dev \
    libarrow-dev \
    libcurl4-openssl-dev \
    libcxxopts-dev \
    libfolly-dev
```

## Building

### Basic Build

For a simple build on your current platform:

```bash
mkdir build
cd build
cmake ..
make
```

### Cross-Platform Build System

LogAI includes a modular build system that can create binaries for multiple platforms:

- Linux x86_64
- Linux ARM64
- macOS ARM64 (Apple Silicon)

#### Requirements for Cross-Platform Building

- Docker (for Linux builds)
- macOS with Apple Silicon (for macOS ARM64 builds)
- Homebrew (for macOS dependency installation)

#### Building for All Platforms

To build for all supported platforms that your environment can handle:

```bash
./build_scripts/build_all.sh
```

This will:
1. Detect your current platform
2. Build native binaries for your platform
3. Use Docker to build binaries for Linux platforms (if Docker is available)
4. Create a combined package with all built binaries

#### Platform-Specific Builds

To build only for a specific platform:

- **Linux ARM64**: `docker build -f build_scripts/Dockerfile.linux-arm64 -t logai-linux-arm64 .`
- **Linux x86_64**: `docker build -f build_scripts/Dockerfile.linux-x86_64 -t logai-linux-x86_64 .`
- **macOS ARM64**: `./build_scripts/build_macos_arm64.sh` (must run on Apple Silicon Mac)

#### Build Output

The build system produces:
- Platform-specific binaries in `bin/<platform>-<arch>/`
- Required libraries in `lib/<platform>-<arch>/`
- Deployment scripts for each platform
- A combined package `logai-release.tar.gz` with all builds

## Usage

The tool provides several commands for log analysis:

1. Parse a log file and extract templates:
```bash
./logai --parse path/to/logfile.log
```

2. Search logs semantically:
```bash
./logai --search "error occurred during database connection"
```

3. Analyze patterns in logs:
```bash
./logai --analyze path/to/logfile.log
```

4. Detect anomalies:
```bash
./logai --detect path/to/logfile.log
```

For more information about available options:
```bash
./logai --help
```

## Deployment

The build system creates deployment scripts for each platform. After extracting the `logai-release.tar.gz` archive:

1. **Automatic deployment**: Run `./deploy.sh` to automatically detect your platform and deploy
2. **Manual platform deployment**:
   - Linux ARM64: `./deploy_linux_arm64.sh`
   - Linux x86_64: `./deploy_linux_x86_64.sh`
   - macOS ARM64: `./deploy_macos_arm64.sh`

The deployment scripts:
1. Copy needed libraries to `~/.local/lib/`
2. Make the binary executable
3. Provide instructions for running the tool

## Running in Docker

A Docker image is available for easy deployment:

```bash
# Build the Docker image
docker-compose build

# Run the container
docker-compose up -d

# Use the CLI tool inside the container
docker exec -it logai-web bash
logai --help
```

## License

MIT License 