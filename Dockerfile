FROM ubuntu:22.04 AS builder

# Avoid prompts from apt
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    git cmake g++ make libssl-dev zlib1g-dev \
    libjsoncpp-dev uuid-dev libmariadb-dev wget curl \
    libcurl4-openssl-dev \
    libboost-all-dev python3 python3-dev python3-pip \
    ninja-build pkg-config unzip \
    nlohmann-json3-dev libjemalloc-dev \
    libgoogle-glog-dev libgflags-dev liblz4-dev libleveldb-dev \
    libtbb-dev libhiredis-dev libspdlog-dev libfmt-dev \
    && rm -rf /var/lib/apt/lists/*

# Install Eigen
WORKDIR /tmp/eigen
RUN wget -q https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.tar.gz \
    && tar -xf eigen-3.4.0.tar.gz \
    && cd eigen-3.4.0 \
    && mkdir build && cd build \
    && cmake .. \
    && make install

# Install Abseil
WORKDIR /tmp/abseil
RUN git clone https://github.com/abseil/abseil-cpp.git \
    && cd abseil-cpp \
    && git checkout 20230125.3 \
    && mkdir build && cd build \
    && cmake -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
            -DCMAKE_CXX_STANDARD=17 \
            -DABSL_ENABLE_INSTALL=ON \
            -DABSL_PROPAGATE_CXX_STD=ON .. \
    && make -j$(nproc) \
    && make install \
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

# Install DuckDB
WORKDIR /tmp/duckdb
RUN git clone https://github.com/duckdb/duckdb.git \
    && cd duckdb \
    && mkdir build && cd build \
    && cmake -DBUILD_SHARED_LIBS=ON \
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON .. \
    && make -j$(nproc) \
    && make install \
    && ldconfig

# Copy source files
WORKDIR /app
COPY . .

# Remove platform-specific code from CMakeLists.txt
RUN sed -i '/if(APPLE)/,/endif()/d' CMakeLists.txt && \
    sed -i 's/if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")/if(FALSE)/g' CMakeLists.txt

# Install pybind11 and other Python dependencies
RUN pip3 install --no-cache-dir pybind11 numpy setuptools wheel twine

# Update CMakeLists.txt to build the Python binding
RUN echo 'find_package(pybind11 REQUIRED)' >> CMakeLists.txt && \
    echo 'pybind11_add_module(logai_cpp src/python_bindings.cpp)' >> CMakeLists.txt && \
    echo 'target_link_libraries(logai_cpp PRIVATE' >> CMakeLists.txt && \
    echo '    nlohmann_json::nlohmann_json' >> CMakeLists.txt && \
    echo '    spdlog::spdlog' >> CMakeLists.txt && \
    echo '    Eigen3::Eigen' >> CMakeLists.txt && \
    echo '    Folly::folly' >> CMakeLists.txt && \
    echo '    ${Boost_LIBRARIES}' >> CMakeLists.txt && \
    echo '    ZLIB::ZLIB' >> CMakeLists.txt && \
    echo '    BZip2::BZip2' >> CMakeLists.txt && \
    echo '    DuckDB::duckdb' >> CMakeLists.txt && \
    echo ')' >> CMakeLists.txt && \
    echo 'install(TARGETS logai_cpp DESTINATION python)' >> CMakeLists.txt

# Build C++ library and Python module
RUN mkdir -p build && cd build \
    && cmake -DCMAKE_BUILD_TYPE=Release \
             -DPYBIND11_PYTHON_VERSION=3 \
             -DCMAKE_POSITION_INDEPENDENT_CODE=ON .. \
    && make -j$(nproc) \
    && make install

# Install Python dependencies needed for the agent
RUN pip3 install --no-cache-dir pydantic openai instructor rich python-dotenv duckdb tqdm pandas numpy pydantic-ai

# Create wheel package
WORKDIR /app/python
# Copy Python module to Python directory and prepare package files
RUN cp /app/build/logai_cpp*.so . && \
    # Create __init__.py to make it a proper package
    echo "from .logai_cpp import *" > __init__.py && \
    # Ensure the logai_agent.py script is executable
    chmod +x logai_agent.py && \
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

# Create startup script
RUN echo '#!/bin/bash\n\
echo "LogAI C++ is available as both a library and CLI tool:"\n\
echo "  - Run logai --help for C++ CLI usage"\n\
echo "  - Run logai-agent --help for Python CLI usage"\n\
echo ""\n\
echo "Python wheel package is available at:"\n\
echo "  /shared/wheels/$(ls /shared/wheels/)"\n\
echo ""\n\
echo "To use the Python CLI on your host machine:"\n\
echo "  1. Copy the wheel file from container to host:"\n\
echo "     docker cp logai-cpp-container:/shared/wheels/$(ls /shared/wheels/) ."\n\
echo "  2. Install on host:"\n\
echo "     pip install $(ls /shared/wheels/)"\n\
echo ""\n\
exec /bin/bash\n' > /start.sh && chmod +x /start.sh

CMD ["/start.sh"] 