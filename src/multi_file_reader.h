#pragma once

#include <string>
#include <vector>
#include <memory>
#include <queue>
#include "file_data_loader.h"

namespace logai {

class MultiFileReader {
public:
    struct FileEntry {
        std::string filename;
        std::string format;
        bool follow;
        bool compressed;
    };
    
    // Initialize with a list of files to read
    explicit MultiFileReader(const std::vector<FileEntry>& files);
    
    // Add a new file to read
    void addFile(const FileEntry& file);
    
    // Remove a file by name
    void removeFile(const std::string& filename);
    
    // Get next log entry from any file, ordered by timestamp
    std::optional<LogParser::LogEntry> nextEntry();
    
    // Check if there are more entries to read
    bool hasMore() const;
    
    // Get list of current files
    std::vector<FileEntry> getFiles() const;
    
    // Get number of entries read so far
    size_t getEntriesRead() const;
    
    // Get number of bytes read so far
    size_t getBytesRead() const;
    
private:
    struct QueueEntry {
        LogParser::LogEntry entry;
        size_t file_index;
        
        bool operator>(const QueueEntry& other) const {
            return entry.timestamp > other.entry.timestamp;
        }
    };
    
    std::vector<FileEntry> files_;
    std::vector<std::unique_ptr<FileDataLoader>> loaders_;
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<>> entry_queue_;
    size_t entries_read_ = 0;
    size_t bytes_read_ = 0;
    
    // Fill the queue with next entries from files
    void fillQueue();
};

} // namespace logai 