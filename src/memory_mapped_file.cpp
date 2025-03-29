#include "memory_mapped_file.h"
#include "simd_scanner.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace logai {

MemoryMappedFile::MemoryMappedFile() 
    : mapped_data_(nullptr), file_size_(0), is_open_(false) {
#ifdef _WIN32
    file_handle_ = INVALID_HANDLE_VALUE;
    mapping_handle_ = nullptr;
#else
    file_descriptor_ = -1;
#endif
}

MemoryMappedFile::MemoryMappedFile(const std::string& path)
    : mapped_data_(nullptr), file_size_(0), is_open_(false) {
#ifdef _WIN32
    file_handle_ = INVALID_HANDLE_VALUE;
    mapping_handle_ = nullptr;
#else
    file_descriptor_ = -1;
#endif
    open(path);
}

MemoryMappedFile::~MemoryMappedFile() {
    close();
}

bool MemoryMappedFile::open(const std::string& path) {
    close();

#ifdef _WIN32
    // Windows implementation
    file_handle_ = CreateFileA(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (file_handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(file_handle_, &file_size)) {
        CloseHandle(file_handle_);
        file_handle_ = INVALID_HANDLE_VALUE;
        return false;
    }
    file_size_ = static_cast<size_t>(file_size.QuadPart);

    mapping_handle_ = CreateFileMappingA(
        file_handle_,
        nullptr,
        PAGE_READONLY,
        0, 0,
        nullptr
    );

    if (mapping_handle_ == nullptr) {
        CloseHandle(file_handle_);
        file_handle_ = INVALID_HANDLE_VALUE;
        return false;
    }

    mapped_data_ = MapViewOfFile(
        mapping_handle_,
        FILE_MAP_READ,
        0, 0, 0
    );

    if (mapped_data_ == nullptr) {
        CloseHandle(mapping_handle_);
        CloseHandle(file_handle_);
        mapping_handle_ = nullptr;
        file_handle_ = INVALID_HANDLE_VALUE;
        return false;
    }
#else
    // POSIX implementation
    file_descriptor_ = ::open(path.c_str(), O_RDONLY);
    if (file_descriptor_ == -1) {
        return false;
    }

    struct stat sb;
    if (fstat(file_descriptor_, &sb) == -1) {
        ::close(file_descriptor_);
        file_descriptor_ = -1;
        return false;
    }
    file_size_ = static_cast<size_t>(sb.st_size);

    mapped_data_ = mmap(
        nullptr,
        file_size_,
        PROT_READ,
        MAP_PRIVATE,
        file_descriptor_,
        0
    );

    if (mapped_data_ == MAP_FAILED) {
        ::close(file_descriptor_);
        file_descriptor_ = -1;
        mapped_data_ = nullptr;
        return false;
    }
#endif

    is_open_ = true;
    return true;
}

void MemoryMappedFile::close() {
    if (!is_open_) return;

#ifdef _WIN32
    if (mapped_data_) {
        UnmapViewOfFile(mapped_data_);
        mapped_data_ = nullptr;
    }
    if (mapping_handle_) {
        CloseHandle(mapping_handle_);
        mapping_handle_ = nullptr;
    }
    if (file_handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(file_handle_);
        file_handle_ = INVALID_HANDLE_VALUE;
    }
#else
    if (mapped_data_) {
        munmap(mapped_data_, file_size_);
        mapped_data_ = nullptr;
    }
    if (file_descriptor_ != -1) {
        ::close(file_descriptor_);
        file_descriptor_ = -1;
    }
#endif

    file_size_ = 0;
    is_open_ = false;
}

const char* MemoryMappedFile::data() const {
    return static_cast<const char*>(mapped_data_);
}

size_t MemoryMappedFile::size() const {
    return file_size_;
}

bool MemoryMappedFile::isOpen() const {
    return is_open_;
}

std::unique_ptr<SimdLogScanner> MemoryMappedFile::getScanner() const {
    if (!is_open_) {
        return nullptr;
    }
    return std::make_unique<SimdLogScanner>(data(), size());
}

} // namespace logai 