#include "multi_file_reader.h"
#include <algorithm>
#include <stdexcept>

namespace logai {

MultiFileReader::MultiFileReader(const std::vector<FileEntry>& files) : files_(files) {
    for (const auto& file : files) {
        FileDataLoaderConfig config;
        config.file_path = file.filename;
        config.format = file.format;
        // Maps follow and compressed fields to appropriate settings
        // These don't exist directly in FileDataLoaderConfig, map to appropriate fields
        if (file.follow) {
            // Handle follow mode settings
        }
        if (file.compressed) {
            config.decompress = file.compressed;
        }
        
        auto loader = std::make_unique<FileDataLoader>(file.filename, config);
        loaders_.push_back(std::move(loader));
    }
    
    fillQueue();
}

void MultiFileReader::addFile(const FileEntry& file) {
    // Check if file already exists
    auto it = std::find_if(files_.begin(), files_.end(),
        [&](const FileEntry& entry) { return entry.filename == file.filename; });
    
    if (it != files_.end()) {
        throw std::runtime_error("File already exists: " + file.filename);
    }
    
    files_.push_back(file);
    
    FileDataLoaderConfig config;
    config.file_path = file.filename;
    config.format = file.format;
    // Maps follow and compressed fields to appropriate settings
    if (file.follow) {
        // Handle follow mode settings
    }
    if (file.compressed) {
        config.decompress = file.compressed;
    }
    
    auto loader = std::make_unique<FileDataLoader>(file.filename, config);
    loaders_.push_back(std::move(loader));
    
    fillQueue();
}

void MultiFileReader::removeFile(const std::string& filename) {
    auto file_it = std::find_if(files_.begin(), files_.end(),
        [&](const FileEntry& entry) { return entry.filename == filename; });
    
    if (file_it == files_.end()) {
        throw std::runtime_error("File not found: " + filename);
    }
    
    size_t index = file_it - files_.begin();
    
    files_.erase(file_it);
    loaders_.erase(loaders_.begin() + index);
    
    // Remove entries from this file from the queue
    std::vector<QueueEntry> remaining_entries;
    while (!entry_queue_.empty()) {
        auto entry = entry_queue_.top();
        entry_queue_.pop();
        
        if (entry.file_index != index) {
            // Adjust file indices for entries from files after the removed one
            if (entry.file_index > index) {
                entry.file_index--;
            }
            remaining_entries.push_back(entry);
        }
    }
    
    for (const auto& entry : remaining_entries) {
        entry_queue_.push(entry);
    }
}

std::optional<LogParser::LogEntry> MultiFileReader::nextEntry() {
    if (entry_queue_.empty()) {
        fillQueue();
        if (entry_queue_.empty()) {
            return std::nullopt;
        }
    }
    
    auto entry = entry_queue_.top();
    entry_queue_.pop();
    
    entries_read_++;
    bytes_read_ += entry.entry.message.size();
    
    // Try to read next entry from the same file
    // Use loadData or streamData instead of nextEntry which doesn't exist in FileDataLoader
    LogParser::LogEntry next_log_entry;
    bool has_next = false;
    
    // Use a temporary vector to get a single entry
    std::vector<LogParser::LogEntry> entries;
    loaders_[entry.file_index]->loadData(entries);
    
    if (!entries.empty()) {
        has_next = true;
        next_log_entry = entries[0];
    }
    
    if (has_next) {
        // Create a QueueEntry and push it
        QueueEntry next_queue_entry;
        next_queue_entry.entry = next_log_entry;
        next_queue_entry.file_index = entry.file_index;
        entry_queue_.push(next_queue_entry);
    }
    
    return entry.entry;
}

bool MultiFileReader::hasMore() const {
    if (!entry_queue_.empty()) {
        return true;
    }
    
    for (const auto& loader : loaders_) {
        // Use get_progress() to check if there's more data
        if (loader->get_progress() < 1.0) {
            return true;
        }
    }
    
    return false;
}

std::vector<MultiFileReader::FileEntry> MultiFileReader::getFiles() const {
    return files_;
}

size_t MultiFileReader::getEntriesRead() const {
    return entries_read_;
}

size_t MultiFileReader::getBytesRead() const {
    return bytes_read_;
}

void MultiFileReader::fillQueue() {
    for (size_t i = 0; i < loaders_.size(); ++i) {
        // Check if there are potentially more entries to read
        if (entry_queue_.empty() || loaders_[i]->get_progress() < 1.0) {
            // Use loadData to get entries instead of nextEntry
            std::vector<LogParser::LogEntry> entries;
            loaders_[i]->loadData(entries);
            
            if (!entries.empty()) {
                // Create a QueueEntry for the first entry
                QueueEntry queue_entry;
                queue_entry.entry = entries[0];
                queue_entry.file_index = i;
                entry_queue_.push(queue_entry);
            }
        }
    }
}

} // namespace logai 