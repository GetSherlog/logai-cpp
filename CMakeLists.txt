cmake_minimum_required(VERSION 3.10)
project(logai)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set platform-specific options
if(APPLE)
    set(PLATFORM_NAME "macos")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
        set(PLATFORM_ARCH "arm64")
        set(PLATFORM_OPTIMIZATION "-mcpu=apple-m1")
    else()
        set(PLATFORM_ARCH "x86_64")
        set(PLATFORM_OPTIMIZATION "-march=native")
    endif()
elseif(UNIX)
    set(PLATFORM_NAME "linux")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
        set(PLATFORM_ARCH "arm64")
        set(PLATFORM_OPTIMIZATION "-march=armv8-a")
    else()
        set(PLATFORM_ARCH "x86_64")
        set(PLATFORM_OPTIMIZATION "-march=native")
    endif()
endif()

# Output information about the build
message(STATUS "Building for platform: ${PLATFORM_NAME}-${PLATFORM_ARCH}")
message(STATUS "Using optimization flags: ${PLATFORM_OPTIMIZATION}")

# Option for static linking
option(BUILD_STATIC "Build with static linking" OFF)

# Find required packages
find_package(CURL REQUIRED)
find_package(DuckDB REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)
find_package(Arrow REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(Folly REQUIRED)
find_package(llama REQUIRED)

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
    src/llama_provider.cpp
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
    src/llama_provider.h
)

# Create executable
add_executable(logai ${SOURCES} ${HEADERS})

# Configure static linking if requested
if(BUILD_STATIC)
    message(STATUS "Building with static linking")
    set_target_properties(logai PROPERTIES LINK_FLAGS "-static")
endif()

# Set output directory based on platform
set(OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/${PLATFORM_NAME}-${PLATFORM_ARCH}")
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
    Arrow::arrow
    Eigen3::Eigen
    Folly::folly
    llama::llama
)

# Include directories
target_include_directories(logai PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
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

# Set rpath for macOS
if(APPLE)
    set_target_properties(logai PROPERTIES
        INSTALL_RPATH "@executable_path/../lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# Print configuration summary
message(STATUS "Configuration Summary:")
message(STATUS "  Platform:          ${PLATFORM_NAME}-${PLATFORM_ARCH}")
message(STATUS "  Optimization:      ${PLATFORM_OPTIMIZATION}")
message(STATUS "  Static linking:    ${BUILD_STATIC}")
message(STATUS "  Output directory:  ${OUTPUT_DIR}")
message(STATUS "  CMAKE_CXX_FLAGS:   ${CMAKE_CXX_FLAGS}")

# Enable testing
enable_testing()

# Print configuration summary
message(STATUS "")
message(STATUS "Configuration summary:")
message(STATUS "  C++ compiler:      ${CMAKE_CXX_COMPILER}")
message(STATUS "  C++ compiler ID:   ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "  C++ compiler flags:${CMAKE_CXX_FLAGS}")
message(STATUS "  Install prefix:    ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Arrow version:     ${ARROW_VERSION}")
message(STATUS "  Parquet version:   ${PARQUET_VERSION}")
message(STATUS "") 