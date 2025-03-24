# Source files (all files in the current directory, excluding test and benchmark files)
file(GLOB SOURCES "*.cpp")
list(FILTER SOURCES EXCLUDE REGEX "perf_test\\.cpp$")
list(FILTER SOURCES EXCLUDE REGEX "large_perf_test\\.cpp$")
list(FILTER SOURCES EXCLUDE REGEX "preprocessor_test\\.cpp$")
list(FILTER SOURCES EXCLUDE REGEX "simd_test\\.cpp$")
list(FILTER SOURCES EXCLUDE REGEX "neon_simd_test\\.cpp$")
list(FILTER SOURCES EXCLUDE REGEX "feature_extractor_test\\.cpp$")
list(FILTER SOURCES EXCLUDE REGEX "logbert_test\\.cpp$")
list(FILTER SOURCES EXCLUDE REGEX "logbert_unit_test\\.cpp$")
list(FILTER SOURCES EXCLUDE REGEX "dbscan_test\\.cpp$")
list(FILTER SOURCES EXCLUDE REGEX "dbscan_kdtree_test\\.cpp$")
list(FILTER SOURCES EXCLUDE REGEX "label_encoder_test\\.cpp$")
list(FILTER SOURCES EXCLUDE REGEX "one_class_svm_test\\.cpp$")
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

# Link with dependencies
target_link_libraries(logai 
    PRIVATE nlohmann_json::nlohmann_json
    PRIVATE PkgConfig::ARROW
    PRIVATE PkgConfig::PARQUET
    PRIVATE absl::strings
    PRIVATE Eigen3::Eigen
    PRIVATE ${CURL_LIBRARIES}
)

# Add performance test executable
add_executable(perf_test perf_test.cpp)
target_link_libraries(perf_test 
    PRIVATE logai
    PRIVATE nlohmann_json::nlohmann_json
    PRIVATE PkgConfig::ARROW
    PRIVATE PkgConfig::PARQUET
    PRIVATE absl::strings
    PRIVATE Eigen3::Eigen
)

# Add CLI executable
add_executable(logai_cli logai.cpp)
target_link_libraries(logai_cli 
    PRIVATE logai
    PRIVATE nlohmann_json::nlohmann_json
    PRIVATE PkgConfig::ARROW
    PRIVATE PkgConfig::PARQUET
    PRIVATE absl::strings
    PRIVATE Eigen3::Eigen
    PRIVATE ${CURL_LIBRARIES}
    PRIVATE cxxopts::cxxopts
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
                target_compile_options(perf_test PRIVATE -march=armv8-a+simd)
                target_compile_definitions(logai PRIVATE USE_NEON_SIMD=1)
                target_compile_definitions(perf_test PRIVATE USE_NEON_SIMD=1)
            endif()
        else()
            check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
            if(COMPILER_SUPPORTS_AVX2)
                target_compile_options(logai PRIVATE -mavx2)
                target_compile_options(perf_test PRIVATE -mavx2)
            endif()
            # Check for SSE4.2 support
            check_cxx_compiler_flag("-msse4.2" COMPILER_SUPPORTS_SSE42)
            if(COMPILER_SUPPORTS_SSE42)
                target_compile_options(logai PRIVATE -msse4.2)
                target_compile_options(perf_test PRIVATE -msse4.2)
            endif()
        endif()
    else()
        # For MSVC, we'll enable AVX2 if available
        if(MSVC)
            target_compile_options(logai PRIVATE /arch:AVX2)
            target_compile_options(perf_test PRIVATE /arch:AVX2)
        endif()
    endif()
else()
    message(STATUS "SIMD optimizations DISABLED")
endif()

# Add pybind11
find_package(pybind11 REQUIRED)

# Python bindings
pybind11_add_module(logai_cpp python_bindings.cpp)
target_link_libraries(logai_cpp PRIVATE ${DEPENDENCIES})

# Install
install(TARGETS logai
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)

install(TARGETS perf_test
        RUNTIME DESTINATION bin)

install(TARGETS logai_cli
        RUNTIME DESTINATION bin)

install(FILES ${HEADERS} DESTINATION include/logai) 