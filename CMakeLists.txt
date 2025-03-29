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
find_package(pybind11 REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)
find_package(Folly REQUIRED)

# We no longer need DuckDB - remove those dependencies

find_package(CURL REQUIRED)

# Add include directories
include_directories(${CURL_INCLUDE_DIRS})

# Source files
file(GLOB SOURCES "src/*.cpp")
file(GLOB HEADERS "src/*.h")

# Create library
add_library(logai
    src/drain_parser.cpp
    src/file_data_loader.cpp
    src/gemini_vectorizer.cpp
    src/llm_interface.cpp
    src/openai_provider.cpp
    src/multi_file_reader.cpp
    src/syslog_parser.cpp
    src/line_parser.cpp
    src/simd_scanner.cpp
    src/simd_string_ops.cpp
    src/memory_mapped_file.cpp
    src/preprocessor.cpp
    src/csv_parser.cpp
    src/json_parser.cpp
    src/regex_parser.cpp
)
target_include_directories(logai PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Link with dependencies
target_link_libraries(logai 
    PRIVATE nlohmann_json::nlohmann_json
    PRIVATE ${CURL_LIBRARIES}
    PRIVATE spdlog::spdlog
)

# Create Python module
pybind11_add_module(logai_cpp MODULE
    src/python_bindings.cpp
)

target_link_libraries(logai_cpp
    PRIVATE logai
    PRIVATE nlohmann_json::nlohmann_json
    PRIVATE ${CURL_LIBRARIES}
    PRIVATE spdlog::spdlog
)

# Set output directory for Python module
set_target_properties(logai_cpp PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/python/logai_cpp"
    PREFIX ""
    SUFFIX ".so"
)

# Install
install(TARGETS logai logai_cpp
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin)

install(FILES ${HEADERS} DESTINATION include/logai)

###############################################################################
# Configuration Summary
###############################################################################
message(STATUS "-------------------------")
message(STATUS "Configuration Summary:")
message(STATUS "  Platform:          ${PLATFORM_NAME}-${PLATFORM_ARCH}")
message(STATUS "  Optimization:      ${PLATFORM_OPTIMIZATION}")
message(STATUS "  Static linking:    ${BUILD_STATIC}")
message(STATUS "  Output directory:  ${CMAKE_BINARY_DIR}")
message(STATUS "  CMAKE_CXX_FLAGS:   ${CMAKE_CXX_FLAGS}")
message(STATUS "  C++ compiler:      ${CMAKE_CXX_COMPILER}")
message(STATUS "  C++ compiler ID:   ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "  Install prefix:    ${CMAKE_INSTALL_PREFIX}")
message(STATUS "-------------------------")
