#ifndef MOCK_PREFERENCES_H
#define MOCK_PREFERENCES_H

#ifdef UNIT_TEST

#include <string>
#include <map>
#include "MockFS.h"  // For String class

// Mock Preferences class for testing
class Preferences {
private:
    static std::map<std::string, std::string> globalStorage;  // Shared across all instances
    std::string currentNamespace;
    bool isOpen;
    bool initFailed;  // For testing initialization failures
    
public:
    Preferences() : isOpen(false), initFailed(false) {}
    
    // Open a namespace
    bool begin(const char* name, bool readOnly = false) {
        if (initFailed) {
            return false;
        }
        
        currentNamespace = name ? name : "";
        isOpen = true;
        return true;
    }
    
    // Close the namespace
    void end() {
        isOpen = false;
        currentNamespace.clear();
    }
    
    // Clear all data in the current namespace
    bool clear() {
        if (!isOpen) return false;
        
        // Remove all keys with current namespace prefix
        std::string prefix = currentNamespace + ":";
        auto it = globalStorage.begin();
        while (it != globalStorage.end()) {
            if (it->first.find(prefix) == 0) {
                it = globalStorage.erase(it);
            } else {
                ++it;
            }
        }
        return true;
    }
    
    // Remove a specific key
    bool remove(const char* key) {
        if (!isOpen || !key) return false;
        
        std::string fullKey = currentNamespace + ":" + key;
        return globalStorage.erase(fullKey) > 0;
    }
    
    // Store a string value
    size_t putString(const char* key, const String& value) {
        if (!isOpen || !key) return 0;
        
        std::string fullKey = currentNamespace + ":" + key;
        std::string strValue = value.toStdString();
        globalStorage[fullKey] = strValue;
        return strValue.length();
    }
    
    // Retrieve a string value
    String getString(const char* key, const String& defaultValue = String()) {
        if (!isOpen || !key) return defaultValue;
        
        std::string fullKey = currentNamespace + ":" + key;
        auto it = globalStorage.find(fullKey);
        if (it != globalStorage.end()) {
            return String(it->second.c_str());
        }
        return defaultValue;
    }
    
    // Store an integer value
    size_t putInt(const char* key, int32_t value) {
        if (!isOpen || !key) return 0;
        
        std::string fullKey = currentNamespace + ":" + key;
        globalStorage[fullKey] = std::to_string(value);
        return sizeof(int32_t);
    }
    
    // Retrieve an integer value
    int32_t getInt(const char* key, int32_t defaultValue = 0) {
        if (!isOpen || !key) return defaultValue;
        
        std::string fullKey = currentNamespace + ":" + key;
        auto it = globalStorage.find(fullKey);
        if (it != globalStorage.end()) {
            try {
                return std::stoi(it->second);
            } catch (...) {
                return defaultValue;
            }
        }
        return defaultValue;
    }
    
    // Check if a key exists
    bool isKey(const char* key) {
        if (!isOpen || !key) return false;
        
        std::string fullKey = currentNamespace + ":" + key;
        return globalStorage.find(fullKey) != globalStorage.end();
    }
    
    // Test helper: Force initialization failure
    void setInitFailed(bool failed) {
        initFailed = failed;
    }
    
    // Test helper: Get all stored keys (for debugging)
    static std::map<std::string, std::string> getAllData() {
        return globalStorage;
    }
    
    // Test helper: Clear all data across all namespaces
    static void clearAll() {
        globalStorage.clear();
    }
};

// Define static storage
std::map<std::string, std::string> Preferences::globalStorage;

#endif // UNIT_TEST

#endif // MOCK_PREFERENCES_H
