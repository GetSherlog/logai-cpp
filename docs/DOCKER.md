# Docker Setup for LogAI C++

This directory contains Docker configuration files to easily build and run the LogAI C++ web server.

## Quick Start

1. Build the Docker image:
```bash
docker build -t logai-cpp .
```

2. Run the container:
```bash
docker run -p 8080:8080 \
  -e LOGAI_MILVUS_HOST=your-milvus-host \
  -e LOGAI_MILVUS_PORT=19530 \
  logai-cpp
```

3. Access the web interface:
- Web Server: http://localhost:8080
- API Documentation: http://localhost:8080/api/docs

## Development Environment

For development, you can use the development Dockerfile:

```bash
# Build development image
docker build -t logai-cpp-dev -f Dockerfile.dev .

# Run development container
docker run -it --name logai-cpp-dev \
  -v "$(pwd):/workspace" \
  -p 8080:8080 \
  -e LOGAI_MILVUS_HOST=your-milvus-host \
  -e LOGAI_MILVUS_PORT=19530 \
  logai-cpp-dev
```

## Environment Variables

- `LOGAI_HOST` - Server host (default: "0.0.0.0")
- `LOGAI_PORT` - Server port (default: 8080)
- `LOGAI_MILVUS_HOST` - Milvus host (default: "localhost")
- `LOGAI_MILVUS_PORT` - Milvus port (default: 19530)

## Docker Compose

For running with all dependencies (including Milvus):

```bash
docker-compose up -d
```

This will start:
- LogAI C++ web server
- Milvus vector database
- Redis (for caching)

## Building from Source

To build the C++ library and Python bindings inside Docker:

```bash
# Build the library
docker build -t logai-cpp-build -f Dockerfile.build .

# Extract the wheel package
docker run --rm -v "$(pwd)/wheels:/wheels" logai-cpp-build
```

The wheel package will be available in the `wheels` directory. 