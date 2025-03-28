FROM ubuntu:22.04 AS builder

# Avoid prompts from apt
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    git cmake g++ make libssl-dev zlib1g-dev \
    libjsoncpp-dev uuid-dev wget curl \
    libcurl4-openssl-dev \
    libboost-all-dev python3 python3-dev python3-pip \
    ninja-build pkg-config unzip \
    nlohmann-json3-dev libjemalloc-dev \
    libgoogle-glog-dev libgflags-dev liblz4-dev \
    libspdlog-dev libfmt-dev \
    python3-pybind11 \
    libprotobuf-dev \
    protobuf-compiler \
    libgrpc++-dev \
    protobuf-compiler-grpc \
    jq \
    && rm -rf /var/lib/apt/lists/*

# Install Python dependencies
RUN pip3 install --no-cache-dir numpy setuptools wheel twine

# Install DuckDB
WORKDIR /tmp/duckdb
RUN wget https://github.com/duckdb/duckdb/releases/download/v0.9.2/libduckdb-src.zip \
    && wget https://github.com/duckdb/duckdb/releases/download/v0.9.2/libduckdb-linux-amd64.zip \
    && unzip -o libduckdb-src.zip \
    && unzip -o libduckdb-linux-amd64.zip \
    && mkdir -p /usr/local/include \
    && cp duckdb.hpp /usr/local/include/ \
    && cp duckdb.h /usr/local/include/ \
    && cp libduckdb.so /usr/local/lib/ \
    && ldconfig

# Install Folly dependencies
WORKDIR /tmp
RUN apt-get update && apt-get install -y \
    autoconf automake binutils-dev libdwarf-dev \
    libevent-dev libsodium-dev libtool \
    libdouble-conversion-dev \
    libzstd-dev libbz2-dev libsnappy-dev \
    libunwind-dev libicu-dev \
    && rm -rf /var/lib/apt/lists/*

# Install FastFloat (needed for Folly)
WORKDIR /tmp/fastfloat
RUN git clone https://github.com/fastfloat/fast_float.git \
    && cd fast_float \
    && mkdir build && cd build \
    && cmake .. \
    && make install

# Install Folly
WORKDIR /tmp/folly
RUN git clone https://github.com/facebook/folly && cd folly/build \
    && cmake -DBUILD_SHARED_LIBS=ON \
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
            -DBUILD_EXAMPLES=OFF \
            -DBUILD_CTL=OFF \
            -DBUILD_TESTING=OFF .. \
    && make all \
    && make install \
    && ldconfig

# Copy source files
WORKDIR /app
COPY . .

# Remove platform-specific code from CMakeLists.txt
RUN sed -i '/if(APPLE)/,/endif()/d' CMakeLists.txt && \
    sed -i 's/if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")/if(FALSE)/g' CMakeLists.txt

# Build C++ library and Python module with memory limits
RUN mkdir -p build && cd build \
    && cmake -DCMAKE_BUILD_TYPE=Release \
             -DPYBIND11_PYTHON_VERSION=3 \
             -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
             -DCMAKE_CXX_FLAGS="-Wno-array-bounds" \
             -DCMAKE_BUILD_PARALLEL_LEVEL=2 .. \
    && make -j2 \
    && make install

# Install Python dependencies needed for the FastAPI server
RUN pip3 install --no-cache-dir pydantic openai instructor rich python-dotenv duckdb tqdm pandas numpy pydantic-ai fastapi uvicorn python-multipart gunicorn google-generativeai anthropic

# Create wheel package
WORKDIR /app/python
# Copy Python module to Python directory and prepare package files
RUN cp /app/build/logai_cpp*.so . && \
    # Create __init__.py to make it a proper package
    echo "from .logai_cpp import *" > __init__.py && \
    # Ensure the logai_agent.py script is executable
    chmod +x logai_agent.py && \
    # Ensure the logai_server.py script is executable
    chmod +x logai_server.py && \
    # Build wheel package
    python3 setup.py bdist_wheel

# Final image
FROM python:3.10-slim

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    libssl-dev zlib1g-dev libjsoncpp-dev uuid-dev \
    libcurl4-openssl-dev libboost-dev libjemalloc-dev \
    libgoogle-glog-dev libgflags-dev liblz4-dev \
    libspdlog-dev libfmt-dev \
    libprotobuf-dev \
    libgrpc++-dev \
    && rm -rf /var/lib/apt/lists/*

# Copy built libraries from builder stage
COPY --from=builder /usr/local/lib/ /usr/local/lib/
COPY --from=builder /usr/local/include/ /usr/local/include/
COPY --from=builder /app/build/bin/ /usr/local/bin/
COPY --from=builder /app/python/dist/ /dist/

# Update library cache
RUN ldconfig

# Install Python package
RUN pip install --no-cache-dir /dist/*.whl

# Create a directory for sharing wheels with the host
WORKDIR /shared
RUN mkdir -p /shared/wheels
COPY --from=builder /app/python/dist/*.whl /shared/wheels/

# Set up working directory
WORKDIR /workspace
RUN mkdir -p /workspace/logs /workspace/uploads

# Copy Python source files
COPY --from=builder /app/python/*.py /workspace/

# Expose port for FastAPI server
EXPOSE 8000

# Set environment variables
ENV PYTHONPATH=/workspace
ENV MILVUS_HOST=milvus
ENV MILVUS_PORT=19530

# Run the FastAPI server
CMD ["uvicorn", "logai_server:app", "--host", "0.0.0.0", "--port", "8000"] 