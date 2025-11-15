#ifndef MOCK_TIME_H
#define MOCK_TIME_H

#ifdef UNIT_TEST

#include <ctime>
#include <cstdint>

// Mock time state
class MockTimeState {
private:
    static unsigned long currentMillis;
    static time_t currentTime;
    
public:
    // Set the mock time in milliseconds (for millis())
    static void setMillis(unsigned long ms) {
        currentMillis = ms;
    }
    
    // Advance the mock time by a number of milliseconds
    static void advanceMillis(unsigned long ms) {
        currentMillis += ms;
    }
    
    // Get the current mock time in milliseconds
    static unsigned long getMillis() {
        return currentMillis;
    }
    
    // Set the mock time in seconds since epoch (for time())
    static void setTime(time_t t) {
        currentTime = t;
    }
    
    // Advance the mock time by a number of seconds
    static void advanceTime(time_t seconds) {
        currentTime += seconds;
    }
    
    // Get the current mock time in seconds since epoch
    static time_t getTime() {
        return currentTime;
    }
    
    // Reset all mock time values
    static void reset() {
        currentMillis = 0;
        currentTime = 0;
    }
};

// Initialize static members
unsigned long MockTimeState::currentMillis = 0;
time_t MockTimeState::currentTime = 0;

// Mock millis() function (Arduino function)
inline unsigned long millis() {
    return MockTimeState::getMillis();
}

// Mock time() function (standard C function)
inline time_t time(time_t* t) {
    time_t current = MockTimeState::getTime();
    if (t != nullptr) {
        *t = current;
    }
    return current;
}

// Mock delay() function (Arduino function)
inline void delay(unsigned long ms) {
    MockTimeState::advanceMillis(ms);
}

// Mock delayMicroseconds() function (Arduino function)
inline void delayMicroseconds(unsigned int us) {
    // For simplicity, advance by 1ms if us >= 1000
    if (us >= 1000) {
        MockTimeState::advanceMillis(us / 1000);
    }
}

// Mock micros() function (Arduino function)
inline unsigned long micros() {
    return MockTimeState::getMillis() * 1000;
}

#endif // UNIT_TEST

#endif // MOCK_TIME_H
