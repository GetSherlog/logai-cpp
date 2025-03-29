#pragma once

#include <string>
#include <memory>
#include "simd_scanner.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace logai {

class MemoryMappedFile {
public:
    MemoryMappedFile();
    explicit MemoryMappedFile(const std::string& path);
    ~MemoryMappedFile();
    
    bool open(const std::string& path);
    void close();
    const char* data() const;
    size_t size() const;
    bool isOpen() const;
    std::unique_ptr<SimdLogScanner> getScanner() const;

private:
    void* mapped_data_;
    size_t file_size_;
    bool is_open_;

#ifdef _WIN32
    HANDLE file_handle_;
    HANDLE mapping_handle_;
#else
    int file_descriptor_;
#endif
};

} 