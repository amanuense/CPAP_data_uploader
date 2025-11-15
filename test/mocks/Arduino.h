#ifndef MOCK_ARDUINO_H
#define MOCK_ARDUINO_H

#ifdef UNIT_TEST

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cmath>
#include "MockTime.h"
#include "MockFS.h"

// Arduino basic types
typedef bool boolean;
typedef uint8_t byte;

// Arduino constants
#define HIGH 0x1
#define LOW  0x0

#define INPUT 0x0
#define OUTPUT 0x1
#define INPUT_PULLUP 0x2

#define PI 3.1415926535897932384626433832795
#define HALF_PI 1.5707963267948966192313216916398
#define TWO_PI 6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105

// Arduino math functions
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#define abs(x) ((x)>0?(x):-(x))
#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))
#define round(x)     ((x)>=0?(long)((x)+0.5):(long)((x)-0.5))
#define radians(deg) ((deg)*DEG_TO_RAD)
#define degrees(rad) ((rad)*RAD_TO_DEG)
#define sq(x) ((x)*(x))

// Arduino bit manipulation
#define lowByte(w) ((uint8_t) ((w) & 0xff))
#define highByte(w) ((uint8_t) ((w) >> 8))

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) (bitvalue ? bitSet(value, bit) : bitClear(value, bit))

#define bit(b) (1UL << (b))

// Mock Serial for logging in tests
class MockSerial {
public:
    void begin(unsigned long baud) {
        // No-op in tests
    }
    
    size_t print(const char* str) {
        printf("%s", str);
        return strlen(str);
    }
    
    size_t print(const String& str) {
        return print(str.c_str());
    }
    
    size_t print(int num) {
        return printf("%d", num);
    }
    
    size_t print(unsigned int num) {
        return printf("%u", num);
    }
    
    size_t print(long num) {
        return printf("%ld", num);
    }
    
    size_t print(unsigned long num) {
        return printf("%lu", num);
    }
    
    size_t print(double num, int digits = 2) {
        return printf("%.*f", digits, num);
    }
    
    size_t println(const char* str) {
        printf("%s\n", str);
        return strlen(str) + 1;
    }
    
    size_t println(const String& str) {
        return println(str.c_str());
    }
    
    size_t println(int num) {
        return printf("%d\n", num);
    }
    
    size_t println(unsigned int num) {
        return printf("%u\n", num);
    }
    
    size_t println(long num) {
        return printf("%ld\n", num);
    }
    
    size_t println(unsigned long num) {
        return printf("%lu\n", num);
    }
    
    size_t println(double num, int digits = 2) {
        return printf("%.*f\n", digits, num);
    }
    
    size_t println() {
        printf("\n");
        return 1;
    }
    
    void flush() {
        fflush(stdout);
    }
};

extern MockSerial Serial;

// Mock random functions
inline long random(long howbig) {
    if (howbig == 0) return 0;
    return rand() % howbig;
}

inline long random(long howsmall, long howbig) {
    if (howsmall >= howbig) return howsmall;
    long diff = howbig - howsmall;
    return random(diff) + howsmall;
}

inline void randomSeed(unsigned long seed) {
    srand(seed);
}

// Mock map function
inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Mock pinMode, digitalWrite, digitalRead (no-ops for testing)
inline void pinMode(uint8_t pin, uint8_t mode) {}
inline void digitalWrite(uint8_t pin, uint8_t val) {}
inline int digitalRead(uint8_t pin) { return LOW; }
inline int analogRead(uint8_t pin) { return 0; }
inline void analogWrite(uint8_t pin, int val) {}

#endif // UNIT_TEST

#endif // MOCK_ARDUINO_H
