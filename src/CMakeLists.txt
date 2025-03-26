# Source files (all files in the current directory, excluding test and benchmark files)
file(GLOB SOURCES "*.cpp")
file(GLOB HEADERS "*.h")

# Create library
add_library(logai
    logai.cpp
    file_data_loader.cpp
    file_data_loader.h
    log_parser.h
    logfmt_parser.cpp
    jsonl_parser.cpp
    syslog_parser.cpp
    log4j_parser.cpp
    cef_parser.cpp
    line_parser.cpp
)
target_include_directories(logai PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Find abseil-cpp package
find_package(absl REQUIRED)

# Find Eigen3 package
find_package(Eigen3 REQUIRED)
include_directories(${EIGEN3_INCLUDE_DIR})

# Find CURL package
find_package(CURL REQUIRED)
include_directories(${CURL_INCLUDE_DIRS})

# Find cxxopts package
find_package(cxxopts REQUIRED)

# Find Folly
find_package(folly REQUIRED)

# Link with dependencies
target_link_libraries(logai 
    PRIVATE nlohmann_json::nlohmann_json
    PRIVATE absl::strings
    PRIVATE Eigen3::Eigen
    PRIVATE ${CURL_LIBRARIES}
    PRIVATE DuckDB::duckdb
    PRIVATE folly
)

# Add CLI executable
add_executable(logai_cli logai.cpp)
target_link_libraries(logai_cli 
    PRIVATE logai
    PRIVATE nlohmann_json::nlohmann_json
    PRIVATE absl::strings
    PRIVATE Eigen3::Eigen
    PRIVATE ${CURL_LIBRARIES}
    PRIVATE cxxopts::cxxopts
    PRIVATE DuckDB::duckdb
    PRIVATE folly
)

# Enable SIMD optimizations
# Add compiler-specific flags for SIMD
if(NOT DEFINED DISABLE_SIMD OR NOT DISABLE_SIMD)
    message(STATUS "SIMD optimizations ENABLED")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        include(CheckCXXCompilerFlag)
        
        # Check if we're on ARM architecture
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)")
            # Check for ARM NEON support
            check_cxx_compiler_flag("-march=armv8-a+simd" COMPILER_SUPPORTS_NEON)
            if(COMPILER_SUPPORTS_NEON)
                message(STATUS "ARM NEON SIMD instructions enabled")
                target_compile_options(logai PRIVATE -march=armv8-a+simd)
                target_compile_definitions(logai PRIVATE USE_NEON_SIMD=1)
            endif()
        else()
            check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
            if(COMPILER_SUPPORTS_AVX2)
                target_compile_options(logai PRIVATE -mavx2)
            endif()
            # Check for SSE4.2 support
            check_cxx_compiler_flag("-msse4.2" COMPILER_SUPPORTS_SSE42)
            if(COMPILER_SUPPORTS_SSE42)
                target_compile_options(logai PRIVATE -msse4.2)
            endif()
        endif()
    else()
        # For MSVC, we'll enable AVX2 if available
        if(MSVC)
            target_compile_options(logai PRIVATE /arch:AVX2)
        endif()
    endif()
else()
    message(STATUS "SIMD optimizations DISABLED")
endif()

# Add pybind11
find_package(pybind11 REQUIRED)

# Python bindings
pybind11_add_module(logai_cpp python_bindings.cpp)
target_link_libraries(logai_cpp PRIVATE 
    logai
    nlohmann_json::nlohmann_json
    absl::strings
    Eigen3::Eigen
    ${CURL_LIBRARIES}
    DuckDB::duckdb
    spdlog
    Boost::iostreams  
    Boost::regex
    Boost::algorithm
    folly
)

# Install
install(TARGETS logai
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)

install(TARGETS logai_cli
        RUNTIME DESTINATION bin)

install(FILES ${HEADERS} DESTINATION include/logai) 