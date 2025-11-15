#ifndef MOCK_FS_H
#define MOCK_FS_H

#ifdef UNIT_TEST

#include <string>
#include <map>
#include <vector>
#include <cstring>

// Mock String class for testing (mimics Arduino String)
class String {
private:
    std::string data;

public:
    String() : data("") {}
    String(const char* str) : data(str ? str : "") {}
    String(const std::string& str) : data(str) {}
    String(int num) : data(std::to_string(num)) {}
    String(unsigned long num) : data(std::to_string(num)) {}
    
    const char* c_str() const { return data.c_str(); }
    size_t length() const { return data.length(); }
    bool isEmpty() const { return data.empty(); }
    
    bool operator==(const String& other) const { return data == other.data; }
    bool operator!=(const String& other) const { return data != other.data; }
    bool operator<(const String& other) const { return data < other.data; }
    
    String& operator=(const char* str) {
        data = str ? str : "";
        return *this;
    }
    
    String& operator=(const std::string& str) {
        data = str;
        return *this;
    }
    
    String operator+(const String& other) const {
        return String(data + other.data);
    }
    
    String& operator+=(const String& other) {
        data += other.data;
        return *this;
    }
    
    char operator[](size_t index) const {
        return index < data.length() ? data[index] : '\0';
    }
    
    int indexOf(char ch) const {
        size_t pos = data.find(ch);
        return pos != std::string::npos ? static_cast<int>(pos) : -1;
    }
    
    String substring(size_t start) const {
        return String(data.substr(start));
    }
    
    String substring(size_t start, size_t end) const {
        return String(data.substr(start, end - start));
    }
    
    bool startsWith(const String& prefix) const {
        return data.find(prefix.data) == 0;
    }
    
    bool endsWith(const String& suffix) const {
        if (suffix.length() > data.length()) return false;
        return data.compare(data.length() - suffix.length(), suffix.length(), suffix.data) == 0;
    }
    
    void toLowerCase() {
        for (char& c : data) {
            c = std::tolower(c);
        }
    }
    
    void toUpperCase() {
        for (char& c : data) {
            c = std::toupper(c);
        }
    }
    
    // For std::map compatibility
    std::string toStdString() const { return data; }
};

// Forward declaration
class MockFile;

// Mock filesystem class
class MockFS {
private:
    struct FileData {
        std::vector<uint8_t> content;
        bool isDirectory;
        
        FileData() : isDirectory(false) {}
    };
    
    std::map<std::string, FileData> files;
    
public:
    MockFS() {}
    
    // Add a file to the mock filesystem
    void addFile(const String& path, const std::vector<uint8_t>& content) {
        FileData data;
        data.content = content;
        data.isDirectory = false;
        files[path.toStdString()] = data;
    }
    
    void addFile(const String& path, const std::string& content) {
        std::vector<uint8_t> bytes(content.begin(), content.end());
        addFile(path, bytes);
    }
    
    // Add a directory to the mock filesystem
    void addDirectory(const String& path) {
        FileData data;
        data.isDirectory = true;
        files[path.toStdString()] = data;
    }
    
    // Check if a file exists
    bool exists(const String& path) {
        return files.find(path.toStdString()) != files.end();
    }
    
    // Remove a file
    bool remove(const String& path) {
        return files.erase(path.toStdString()) > 0;
    }
    
    // Create a directory
    bool mkdir(const String& path) {
        addDirectory(path);
        return true;
    }
    
    // Remove a directory
    bool rmdir(const String& path) {
        return remove(path);
    }
    
    // Rename a file
    bool rename(const String& pathFrom, const String& pathTo) {
        auto it = files.find(pathFrom.toStdString());
        if (it == files.end()) {
            return false;
        }
        
        FileData data = it->second;
        files.erase(it);
        files[pathTo.toStdString()] = data;
        return true;
    }
    
    // Open a file
    MockFile open(const String& path, const char* mode = "r");
    
    // Get file content (for testing)
    std::vector<uint8_t> getFileContent(const String& path) {
        auto it = files.find(path.toStdString());
        if (it != files.end() && !it->second.isDirectory) {
            return it->second.content;
        }
        return std::vector<uint8_t>();
    }
    
    // List files in a directory
    std::vector<String> listDir(const String& path) {
        std::vector<String> result;
        std::string dirPath = path.toStdString();
        if (!dirPath.empty() && dirPath.back() != '/') {
            dirPath += '/';
        }
        
        for (const auto& entry : files) {
            if (entry.first.find(dirPath) == 0) {
                std::string relativePath = entry.first.substr(dirPath.length());
                size_t slashPos = relativePath.find('/');
                if (slashPos == std::string::npos) {
                    // File directly in this directory
                    result.push_back(String(relativePath.c_str()));
                } else {
                    // Subdirectory
                    std::string subdir = relativePath.substr(0, slashPos);
                    String subdirStr(subdir.c_str());
                    // Add only if not already in result
                    bool found = false;
                    for (const auto& r : result) {
                        if (r == subdirStr) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        result.push_back(subdirStr);
                    }
                }
            }
        }
        
        return result;
    }
    
    // Clear all files (for test cleanup)
    void clear() {
        files.clear();
    }
    
    // Internal method to set file content (used by MockFile)
    void setFileContent(const String& path, const std::vector<uint8_t>& content) {
        FileData data;
        data.content = content;
        data.isDirectory = false;
        files[path.toStdString()] = data;
    }
};

// Mock file class
class MockFile {
private:
    MockFS* fs;
    String path;
    std::vector<uint8_t> content;
    size_t filePosition;
    bool isOpen;
    bool isWriteMode;
    bool isDirectory;
    
public:
    MockFile() : fs(nullptr), filePosition(0), isOpen(false), isWriteMode(false), isDirectory(false) {}
    
    MockFile(MockFS* filesystem, const String& filePath, const char* mode)
        : fs(filesystem), path(filePath), filePosition(0), isOpen(false), isDirectory(false) {
        
        isWriteMode = (mode && (strchr(mode, 'w') != nullptr || strchr(mode, 'a') != nullptr));
        
        if (fs && fs->exists(path)) {
            content = fs->getFileContent(path);
            isOpen = true;
            if (mode && strchr(mode, 'a') != nullptr) {
                // Append mode: filePosition at end
                filePosition = content.size();
            }
        } else if (isWriteMode) {
            // Write mode: create new file
            isOpen = true;
        }
    }
    
    operator bool() const {
        return isOpen;
    }
    
    size_t size() {
        return content.size();
    }
    
    bool isDir() {
        return isDirectory;
    }
    
    String name() {
        size_t lastSlash = path.toStdString().find_last_of('/');
        if (lastSlash != std::string::npos) {
            return String(path.toStdString().substr(lastSlash + 1).c_str());
        }
        return path;
    }
    
    size_t read(uint8_t* buffer, size_t len) {
        if (!isOpen || filePosition >= content.size()) {
            return 0;
        }
        
        size_t available = content.size() - filePosition;
        size_t toRead = (len < available) ? len : available;
        
        memcpy(buffer, content.data() + filePosition, toRead);
        filePosition += toRead;
        
        return toRead;
    }
    
    int read() {
        if (!isOpen || filePosition >= content.size()) {
            return -1;
        }
        return content[filePosition++];
    }
    
    size_t write(const uint8_t* buffer, size_t len) {
        if (!isOpen || !isWriteMode) {
            return 0;
        }
        
        // Ensure content is large enough
        if (filePosition + len > content.size()) {
            content.resize(filePosition + len);
        }
        
        memcpy(content.data() + filePosition, buffer, len);
        filePosition += len;
        
        return len;
    }
    
    size_t write(uint8_t byte) {
        return write(&byte, 1);
    }
    
    size_t print(const char* str) {
        return write((const uint8_t*)str, strlen(str));
    }
    
    size_t println(const char* str) {
        size_t written = print(str);
        written += write((const uint8_t*)"\n", 1);
        return written;
    }
    
    bool seek(size_t pos) {
        if (pos <= content.size()) {
            filePosition = pos;
            return true;
        }
        return false;
    }
    
    size_t position() {
        return filePosition;
    }
    
    int available() {
        if (!isOpen || filePosition >= content.size()) {
            return 0;
        }
        return content.size() - filePosition;
    }
    
    void close() {
        if (isOpen && isWriteMode && fs) {
            // Write content back to filesystem
            fs->setFileContent(path, content);
        }
        isOpen = false;
    }
};

// Implementation of MockFS::open (needs MockFile to be defined)
inline MockFile MockFS::open(const String& path, const char* mode) {
    return MockFile(this, path, mode);
}

// Mock fs namespace to match Arduino FS.h
namespace fs {
    typedef MockFS FS;
    typedef MockFile File;
}

// File mode constants
#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"

// Mock yield function
inline void yield() {
    // No-op in tests
}

#endif // UNIT_TEST

#endif // MOCK_FS_H
