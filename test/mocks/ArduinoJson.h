#ifndef MOCK_ARDUINO_JSON_H
#define MOCK_ARDUINO_JSON_H

#ifdef UNIT_TEST

#include <string>
#include <map>
#include <vector>
#include <cstdint>

// Undefine Arduino macros that conflict with C++ standard library
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <sstream>
#include "Arduino.h"

// Make File available in global namespace for compatibility
using File = fs::File;

// Forward declarations
class JsonObject;
class JsonArray;
class JsonVariant;
class JsonPair;

// Mock JSON variant - simplified
class JsonVariant {
public:
    std::string stringValue;
    long longValue;
    bool isString;
    bool hasValue;  // Track if this variant actually has a value
    
    JsonVariant() : longValue(0), isString(false), hasValue(false) {}
    JsonVariant(const char* val) : stringValue(val ? val : ""), longValue(0), isString(true), hasValue(true) {}
    JsonVariant(const std::string& val) : stringValue(val), longValue(0), isString(true), hasValue(true) {}
    JsonVariant(const String& val) : stringValue(val.c_str()), longValue(0), isString(true), hasValue(true) {}
    JsonVariant(int val) : longValue(val), isString(false), hasValue(true) {}
    JsonVariant(long val) : longValue(val), isString(false), hasValue(true) {}
    JsonVariant(unsigned long val) : longValue(static_cast<long>(val)), isString(false), hasValue(true) {}
    
    bool isNull() const { return !hasValue; }
    
    template<typename T>
    T as() const;
    
    String operator|(const char* defaultValue) const {
        if (hasValue && isString && !stringValue.empty()) {
            return String(stringValue.c_str());
        }
        return String(defaultValue);
    }
    
    int operator|(int defaultValue) const {
        if (hasValue && !isString) {
            return static_cast<int>(longValue);
        }
        return defaultValue;
    }
    
    long operator|(long defaultValue) const {
        if (hasValue && !isString) {
            return longValue;
        }
        return defaultValue;
    }
    
    unsigned long operator|(unsigned long defaultValue) const {
        if (hasValue && !isString) {
            return static_cast<unsigned long>(longValue);
        }
        return defaultValue;
    }
};

// Template specializations for as()
template<> inline const char* JsonVariant::as<const char*>() const {
    return stringValue.c_str();
}

template<> inline int JsonVariant::as<int>() const {
    return static_cast<int>(longValue);
}

template<> inline long JsonVariant::as<long>() const {
    return longValue;
}

template<> inline unsigned long JsonVariant::as<unsigned long>() const {
    return static_cast<unsigned long>(longValue);
}

// Mock JSON pair
class JsonPair {
public:
    std::string keyStr;
    JsonVariant val;
    
    JsonPair(const std::string& k, const JsonVariant& v) : keyStr(k), val(v) {}
    
    class Key {
    public:
        std::string str;
        Key(const std::string& s) : str(s) {}
        const char* c_str() const { return str.c_str(); }
    };
    
    Key key() const { return Key(keyStr); }
    JsonVariant value() const { return val; }
};

// Mock JSON object
class JsonObject {
public:
    std::map<std::string, JsonVariant>* data;
    
    JsonObject() : data(nullptr) {}
    JsonObject(std::map<std::string, JsonVariant>* d) : data(d) {}
    
    bool isNull() const { return data == nullptr; }
    
    JsonVariant operator[](const char* key) const {
        if (!data) return JsonVariant();
        auto it = data->find(key);
        if (it != data->end()) {
            return it->second;
        }
        return JsonVariant();
    }
    
    JsonVariant& operator[](const char* key) {
        if (!data) {
            static JsonVariant nullVar;
            return nullVar;
        }
        return (*data)[key];
    }
    
    // Iterator support
    std::map<std::string, JsonVariant>::iterator begin() { 
        return data ? data->begin() : std::map<std::string, JsonVariant>::iterator(); 
    }
    std::map<std::string, JsonVariant>::iterator end() { 
        return data ? data->end() : std::map<std::string, JsonVariant>::iterator(); 
    }
};

// Mock JSON array
class JsonArray {
public:
    std::vector<JsonVariant>* data;
    
    JsonArray() : data(nullptr) {}
    JsonArray(std::vector<JsonVariant>* d) : data(d) {}
    
    bool isNull() const { return data == nullptr; }
    
    void add(const char* value) {
        if (data) data->push_back(JsonVariant(value));
    }
    
    void add(const String& value) {
        if (data) data->push_back(JsonVariant(value));
    }
    
    void add(int value) {
        if (data) data->push_back(JsonVariant(value));
    }
    
    // Iterator support
    std::vector<JsonVariant>::iterator begin() { 
        return data ? data->begin() : std::vector<JsonVariant>::iterator(); 
    }
    std::vector<JsonVariant>::iterator end() { 
        return data ? data->end() : std::vector<JsonVariant>::iterator(); 
    }
};

// Mock JSON document base
class JsonDocumentBase {
public:
    std::map<std::string, JsonVariant> objectData;
    std::map<std::string, std::map<std::string, JsonVariant>> nestedObjects;
    std::map<std::string, std::vector<JsonVariant>> nestedArrays;
    bool parseError;
    
    JsonDocumentBase() : parseError(false) {}
    
    JsonVariant& operator[](const char* key) {
        return objectData[key];
    }
    
    JsonObject getObject(const char* key) {
        auto it = nestedObjects.find(key);
        if (it != nestedObjects.end()) {
            return JsonObject(&it->second);
        }
        return JsonObject(nullptr);
    }
    
    JsonArray getArray(const char* key) {
        auto it = nestedArrays.find(key);
        if (it != nestedArrays.end()) {
            return JsonArray(&it->second);
        }
        return JsonArray(nullptr);
    }
    
    JsonObject createNestedObject(const char* key) {
        nestedObjects[key] = std::map<std::string, JsonVariant>();
        return JsonObject(&nestedObjects[key]);
    }
    
    JsonArray createNestedArray(const char* key) {
        nestedArrays[key] = std::vector<JsonVariant>();
        return JsonArray(&nestedArrays[key]);
    }
    
    void setParseError(bool error) {
        parseError = error;
    }
    
    bool hasParseError() const {
        return parseError;
    }
};

// Mock static JSON document
template<size_t SIZE>
class StaticJsonDocument : public JsonDocumentBase {
public:
    StaticJsonDocument() : JsonDocumentBase() {}
};

// Mock dynamic JSON document
class DynamicJsonDocument : public JsonDocumentBase {
public:
    size_t capacity;
    
    DynamicJsonDocument(size_t cap) : JsonDocumentBase(), capacity(cap) {}
};

// Mock deserialization error
class DeserializationError {
public:
    bool error;
    std::string message;
    
    DeserializationError() : error(false), message("") {}
    DeserializationError(bool err, const std::string& msg = "") : error(err), message(msg) {}
    
    operator bool() const {
        return error;
    }
    
    const char* c_str() const {
        return message.c_str();
    }
    
    static DeserializationError Ok() {
        return DeserializationError(false);
    }
    
    static DeserializationError InvalidInput() {
        return DeserializationError(true, "Invalid input");
    }
    
    static DeserializationError NoMemory() {
        return DeserializationError(true, "No memory");
    }
};

// Mock deserializeJson function - simplified parser
template<typename T>
DeserializationError deserializeJson(T& doc, fs::File& file) {
    // Read file content
    std::string content;
    while (file.available()) {
        char c = file.read();
        content += c;
    }
    
    // Simple JSON parser for testing
    if (content.empty() || content[0] != '{') {
        return DeserializationError::InvalidInput();
    }
    
    // Parse key-value pairs
    size_t pos = 1;
    while (pos < content.length()) {
        // Skip whitespace
        while (pos < content.length() && (content[pos] == ' ' || content[pos] == '\n' || content[pos] == '\r' || content[pos] == '\t')) {
            pos++;
        }
        
        if (pos >= content.length() || content[pos] == '}') {
            break;
        }
        
        // Skip comma
        if (content[pos] == ',') {
            pos++;
            continue;
        }
        
        // Parse key
        if (content[pos] != '"') {
            return DeserializationError::InvalidInput();
        }
        pos++;
        
        std::string key;
        while (pos < content.length() && content[pos] != '"') {
            key += content[pos++];
        }
        pos++; // Skip closing quote
        
        // Skip whitespace and colon
        while (pos < content.length() && (content[pos] == ' ' || content[pos] == ':')) {
            pos++;
        }
        
        // Parse value
        if (content[pos] == '"') {
            // String value
            pos++;
            std::string value;
            while (pos < content.length() && content[pos] != '"') {
                value += content[pos++];
            }
            pos++; // Skip closing quote
            doc[key.c_str()] = JsonVariant(value.c_str());
        } else if (content[pos] == '[') {
            // Array value
            pos++;
            std::vector<JsonVariant> arr;
            while (pos < content.length() && content[pos] != ']') {
                // Skip whitespace and commas
                while (pos < content.length() && (content[pos] == ' ' || content[pos] == ',' || content[pos] == '\n')) {
                    pos++;
                }
                if (content[pos] == ']') break;
                
                if (content[pos] == '"') {
                    pos++;
                    std::string value;
                    while (pos < content.length() && content[pos] != '"') {
                        value += content[pos++];
                    }
                    pos++;
                    arr.push_back(JsonVariant(value.c_str()));
                }
            }
            pos++; // Skip closing bracket
            doc.nestedArrays[key] = arr;
        } else if (content[pos] == '{') {
            // Nested object
            pos++;
            std::map<std::string, JsonVariant> obj;
            while (pos < content.length() && content[pos] != '}') {
                // Skip whitespace and commas
                while (pos < content.length() && (content[pos] == ' ' || content[pos] == ',' || content[pos] == '\n')) {
                    pos++;
                }
                if (content[pos] == '}') break;
                
                // Parse nested key
                if (content[pos] == '"') {
                    pos++;
                    std::string nestedKey;
                    while (pos < content.length() && content[pos] != '"') {
                        nestedKey += content[pos++];
                    }
                    pos++;
                    
                    // Skip colon
                    while (pos < content.length() && (content[pos] == ' ' || content[pos] == ':')) {
                        pos++;
                    }
                    
                    // Parse nested value
                    if (content[pos] == '"') {
                        pos++;
                        std::string nestedValue;
                        while (pos < content.length() && content[pos] != '"') {
                            nestedValue += content[pos++];
                        }
                        pos++;
                        obj[nestedKey] = JsonVariant(nestedValue.c_str());
                    }
                }
            }
            pos++; // Skip closing brace
            doc.nestedObjects[key] = obj;
        } else if (content[pos] == '-' || (content[pos] >= '0' && content[pos] <= '9')) {
            // Numeric value
            std::string numStr;
            if (content[pos] == '-') {
                numStr += '-';
                pos++;
            }
            while (pos < content.length() && content[pos] >= '0' && content[pos] <= '9') {
                numStr += content[pos++];
            }
            
            if (!numStr.empty() && numStr != "-") {
                long value = std::stol(numStr);
                doc[key.c_str()] = JsonVariant(value);
            }
        }
    }
    
    return DeserializationError::Ok();
}

// Mock serializeJson function
template<typename T>
size_t serializeJson(T& doc, fs::File& file) {
    std::ostringstream oss;
    oss << "{";
    
    bool first = true;
    
    // Serialize simple values
    for (const auto& pair : doc.objectData) {
        if (!first) oss << ",";
        first = false;
        oss << "\"" << pair.first << "\":";
        
        if (pair.second.isString) {
            oss << "\"" << pair.second.stringValue << "\"";
        } else {
            oss << pair.second.longValue;
        }
    }
    
    // Serialize nested objects
    for (const auto& objPair : doc.nestedObjects) {
        if (!first) oss << ",";
        first = false;
        oss << "\"" << objPair.first << "\":{";
        
        bool objFirst = true;
        for (const auto& pair : objPair.second) {
            if (!objFirst) oss << ",";
            objFirst = false;
            oss << "\"" << pair.first << "\":\"" << pair.second.stringValue << "\"";
        }
        oss << "}";
    }
    
    // Serialize nested arrays
    for (const auto& arrPair : doc.nestedArrays) {
        if (!first) oss << ",";
        first = false;
        oss << "\"" << arrPair.first << "\":[";
        
        bool arrFirst = true;
        for (const auto& item : arrPair.second) {
            if (!arrFirst) oss << ",";
            arrFirst = false;
            oss << "\"" << item.stringValue << "\"";
        }
        oss << "]";
    }
    
    oss << "}";
    
    std::string json = oss.str();
    size_t written = file.write((const uint8_t*)json.c_str(), json.length());
    return written;
}

#endif // UNIT_TEST

#endif // MOCK_ARDUINO_JSON_H
