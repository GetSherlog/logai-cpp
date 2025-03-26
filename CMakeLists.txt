cmake_minimum_required(VERSION 3.10)
project(logai)

# Add our CMake module path
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set platform-specific options for Linux only
set(PLATFORM_NAME "linux")
set(PLATFORM_ARCH "x86_64")
set(PLATFORM_OPTIMIZATION "-march=native")

# Output information about the build
message(STATUS "Building for platform: ${PLATFORM_NAME}-${PLATFORM_ARCH}")
message(STATUS "Using optimization flags: ${PLATFORM_OPTIMIZATION}")

# Option for static linking
option(BUILD_STATIC "Build with static linking" OFF)

# Find required packages
find_package(CURL REQUIRED)

# Set up DuckDB manually
set(DUCKDB_DIR "${CMAKE_SOURCE_DIR}/duckdb-lib")
if(EXISTS "${DUCKDB_DIR}/include")
    set(DUCKDB_INCLUDE_DIR "${DUCKDB_DIR}/include")
elseif(EXISTS "${DUCKDB_DIR}")
    # DuckDB might have headers directly in the lib directory
    set(DUCKDB_INCLUDE_DIR "${DUCKDB_DIR}")
else()
    message(FATAL_ERROR "DuckDB directory not found at ${DUCKDB_DIR}")
endif()

if(EXISTS "${DUCKDB_DIR}/libduckdb.a")
    set(DUCKDB_LIBRARY "${DUCKDB_DIR}/libduckdb.a")
elseif(EXISTS "${DUCKDB_DIR}/duckdb.lib")
    set(DUCKDB_LIBRARY "${DUCKDB_DIR}/duckdb.lib")
elseif(EXISTS "${DUCKDB_DIR}/libduckdb.so")
    set(DUCKDB_LIBRARY "${DUCKDB_DIR}/libduckdb.so")
elseif(EXISTS "${DUCKDB_DIR}/libduckdb.dylib")
    set(DUCKDB_LIBRARY "${DUCKDB_DIR}/libduckdb.dylib")
else()
    message(FATAL_ERROR "DuckDB library not found in ${DUCKDB_DIR}")
endif()

message(STATUS "Using DuckDB include directory: ${DUCKDB_INCLUDE_DIR}")
message(STATUS "Using DuckDB library: ${DUCKDB_LIBRARY}")

# Create imported target for DuckDB
add_library(DuckDB::duckdb STATIC IMPORTED)
set_target_properties(DuckDB::duckdb PROPERTIES
    IMPORTED_LOCATION "${DUCKDB_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${DUCKDB_INCLUDE_DIR}"
)

# Try to find nlohmann_json, if not found we'll use FetchContent
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found, fetching from source")
    include(FetchContent)
    FetchContent_Declare(json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(json)
endif()

find_package(spdlog REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(Folly REQUIRED)
find_package(Boost REQUIRED COMPONENTS iostreams)
find_package(ZLIB REQUIRED)
find_package(BZip2 REQUIRED)

# Add source files
set(SOURCES
    src/logai.cpp
    src/drain_parser.cpp
    src/gemini_vectorizer.cpp
    src/template_store.cpp
    src/feature_extractor.cpp
    src/file_data_loader.cpp
    src/dbscan_clustering_kdtree.cpp
    src/one_class_svm.cpp
    src/duckdb_store.cpp
    src/llm_interface.cpp
    src/openai_provider.cpp
)

# Add header files
set(HEADERS
    src/drain_parser.h
    src/gemini_vectorizer.h
    src/template_store.h
    src/feature_extractor.h
    src/file_data_loader.h
    src/dbscan_clustering_kdtree.h
    src/one_class_svm.h
    src/duckdb_store.h
    src/llm_interface.h
    src/llm_provider.h
    src/openai_provider.h
)

# Create executable
add_executable(logai ${SOURCES} ${HEADERS})

# Configure static linking if requested
if(BUILD_STATIC)
    message(STATUS "Building with static linking")
    set_target_properties(logai PROPERTIES LINK_FLAGS "-static")
endif()

# Set output directory
set(OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin")
set_target_properties(logai PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OUTPUT_DIR}"
)

# Link libraries
target_link_libraries(logai PRIVATE
    CURL::libcurl
    DuckDB::duckdb
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    Eigen3::Eigen
    Folly::folly
    ${Boost_LIBRARIES}
    ZLIB::ZLIB
    BZip2::BZip2
)

# Include directories
target_include_directories(logai PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${Boost_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    ${BZIP2_INCLUDE_DIRS}
)

# Add compiler warnings and optimizations
if(MSVC)
    target_compile_options(logai PRIVATE /W4)
else()
    target_compile_options(logai PRIVATE -Wall -Wextra -Wpedantic ${PLATFORM_OPTIMIZATION})
endif()

# Installation rules
install(TARGETS logai
    RUNTIME DESTINATION bin
)

# Add pybind11 support
find_package(pybind11 REQUIRED)

# Print configuration summary
message(STATUS "Configuration Summary:")
message(STATUS "  Platform:          ${PLATFORM_NAME}-${PLATFORM_ARCH}")
message(STATUS "  Optimization:      ${PLATFORM_OPTIMIZATION}")
message(STATUS "  Static linking:    ${BUILD_STATIC}")
message(STATUS "  Output directory:  ${OUTPUT_DIR}")
message(STATUS "  CMAKE_CXX_FLAGS:   ${CMAKE_CXX_FLAGS}")

# Print configuration summary
message(STATUS "")
message(STATUS "Configuration summary:")
message(STATUS "  C++ compiler:      ${CMAKE_CXX_COMPILER}")
message(STATUS "  C++ compiler ID:   ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "  C++ compiler flags:${CMAKE_CXX_FLAGS}")
message(STATUS "  Install prefix:    ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  DuckDB version:    ${DUCKDB_VERSION}")
message(STATUS "") 