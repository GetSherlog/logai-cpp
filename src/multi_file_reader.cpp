#include "multi_file_reader.h"
#include <algorithm>
#include <stdexcept>

namespace logai {

MultiFileReader::MultiFileReader(const std::vector<FileEntry>& files) : files_(files) {
    for (const auto& file : files) {
        FileDataLoaderConfig config;
        config.filename = file.filename;
        config.format = file.format;
        config.follow = file.follow;
        config.compressed = file.compressed;
        
        auto loader = std::make_unique<FileDataLoader>(config);
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
    config.filename = file.filename;
    config.format = file.format;
    config.follow = file.follow;
    config.compressed = file.compressed;
    
    auto loader = std::make_unique<FileDataLoader>(config);
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
    auto next_entry = loaders_[entry.file_index]->nextEntry();
    if (next_entry) {
        entry_queue_.push({*next_entry, entry.file_index});
    }
    
    return entry.entry;
}

bool MultiFileReader::hasMore() const {
    if (!entry_queue_.empty()) {
        return true;
    }
    
    for (const auto& loader : loaders_) {
        if (loader->hasMore()) {
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
        if (entry_queue_.empty() || !loaders_[i]->hasMore()) {
            auto entry = loaders_[i]->nextEntry();
            if (entry) {
                entry_queue_.push({*entry, i});
            }
        }
    }
}

} // namespace logai 