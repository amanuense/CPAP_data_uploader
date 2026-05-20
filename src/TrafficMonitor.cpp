#include "TrafficMonitor.h"
#include "Logger.h"
#include "driver/pcnt.h"

// PCNT unit and channel used for bus activity detection
#define TRAFFIC_PCNT_UNIT   PCNT_UNIT_0
#define TRAFFIC_PCNT_CHANNEL PCNT_CHANNEL_0

TrafficMonitor::TrafficMonitor()
    : _pin(-1)
    , _initialized(false)
    , _lastSampleTime(0)
    , _lastSampleActive(false)
    , _lastPulseCount(0)
    , _consecutiveIdleMs(0)
    , _lastSecondTime(0)
    , _secondPulseAccumulator(0)
    , _sampleHead(0)
    , _sampleCount(0)
    , _longestIdleMs(0)
    , _totalActiveSamples(0)
    , _totalIdleSamples(0)
{
    memset(_sampleBuffer, 0, sizeof(_sampleBuffer));
}

TrafficMonitor& TrafficMonitor::getInstance() {
    extern TrafficMonitor trafficMonitor;
    return trafficMonitor;
}

void TrafficMonitor::begin(int pin) {
    _pin = pin;
    
    // Configure GPIO as input (floating - rely on external pull-ups on SD bus)
    pinMode(_pin, INPUT);
    
    // Configure PCNT unit
    pcnt_config_t pcntConfig = {};
    pcntConfig.pulse_gpio_num = _pin;
    pcntConfig.ctrl_gpio_num = PCNT_PIN_NOT_USED;
    pcntConfig.channel = TRAFFIC_PCNT_CHANNEL;
    pcntConfig.unit = TRAFFIC_PCNT_UNIT;
    pcntConfig.pos_mode = PCNT_COUNT_INC;   // Count on rising edge
    pcntConfig.neg_mode = PCNT_COUNT_INC;   // Count on falling edge
    pcntConfig.lctrl_mode = PCNT_MODE_KEEP;
    pcntConfig.hctrl_mode = PCNT_MODE_KEEP;
    pcntConfig.counter_h_lim = 32767;       // Max 16-bit signed
    pcntConfig.counter_l_lim = 0;
    
    esp_err_t err = pcnt_unit_config(&pcntConfig);
    if (err != ESP_OK) {
        LOG_ERRORF("PCNT config failed: %d", err);
        return;
    }
    
    // Set glitch filter to ignore pulses < ~100ns (filter value = 10 APB clock cycles)
    err = pcnt_set_filter_value(TRAFFIC_PCNT_UNIT, 10);
    if (err != ESP_OK) {
        LOG_WARNF("PCNT filter config failed: %d", err);
    }
    pcnt_filter_enable(TRAFFIC_PCNT_UNIT);
    
    // Clear and start counter
    pcnt_counter_pause(TRAFFIC_PCNT_UNIT);
    pcnt_counter_clear(TRAFFIC_PCNT_UNIT);
    pcnt_counter_resume(TRAFFIC_PCNT_UNIT);
    
    _lastSampleTime = millis();
    _lastSecondTime = millis();
    _initialized = true;
    
    LOGF("TrafficMonitor initialized on GPIO %d (PCNT unit %d)", _pin, TRAFFIC_PCNT_UNIT);
}

void TrafficMonitor::update() {
    if (!_initialized) return;
    
    unsigned long now = millis();
    
    // Sample every ~100ms
    if (now - _lastSampleTime < SAMPLE_INTERVAL_MS) return;
    
    uint32_t elapsed = now - _lastSampleTime;
    _lastSampleTime = now;
    
    // Read and clear PCNT counter
    int16_t count = 0;
    pcnt_get_counter_value(TRAFFIC_PCNT_UNIT, &count);
    pcnt_counter_clear(TRAFFIC_PCNT_UNIT);
    
    _lastPulseCount = (count > 0) ? (uint16_t)count : 0;
    _lastSampleActive = (_lastPulseCount > 0);
    
    // Update idle tracking
    if (_lastSampleActive) {
        _consecutiveIdleMs = 0;
    } else {
        _consecutiveIdleMs += elapsed;
        if (_consecutiveIdleMs > _longestIdleMs) {
            _longestIdleMs = _consecutiveIdleMs;
        }
    }
    
    // Aggregate into 1-second windows for sample buffer
    _secondPulseAccumulator += _lastPulseCount;
    
    if (now - _lastSecondTime >= 1000) {
        uint32_t ts = now / 1000;
        pushSample(ts, (uint16_t)min(_secondPulseAccumulator, (uint32_t)65535));
        
        // Update per-second statistics
        if (_secondPulseAccumulator > 0) {
            _totalActiveSamples++;
        } else {
            _totalIdleSamples++;
        }
        
        _secondPulseAccumulator = 0;
        _lastSecondTime = now;
    }
}

bool TrafficMonitor::isBusy() {
    return _lastSampleActive;
}

bool TrafficMonitor::isIdleFor(uint32_t ms) {
    return _consecutiveIdleMs >= ms;
}

uint32_t TrafficMonitor::getConsecutiveIdleMs() {
    return _consecutiveIdleMs;
}

void TrafficMonitor::resetIdleTracking() {
    _consecutiveIdleMs = 0;
}

const ActivitySample* TrafficMonitor::getSampleBuffer() const {
    return _sampleBuffer;
}

int TrafficMonitor::getSampleCount() const {
    return _sampleCount;
}

int TrafficMonitor::getSampleHead() const {
    return _sampleHead;
}

uint32_t TrafficMonitor::getLongestIdleMs() const {
    return _longestIdleMs;
}

uint32_t TrafficMonitor::getTotalActiveSamples() const {
    return _totalActiveSamples;
}

uint32_t TrafficMonitor::getTotalIdleSamples() const {
    return _totalIdleSamples;
}

uint16_t TrafficMonitor::getLastPulseCount() const {
    return _lastPulseCount;
}

void TrafficMonitor::resetStatistics() {
    _longestIdleMs = 0;
    _totalActiveSamples = 0;
    _totalIdleSamples = 0;
    _sampleHead = 0;
    _sampleCount = 0;
    _secondPulseAccumulator = 0;
    _lastSecondTime = millis();
    memset(_sampleBuffer, 0, sizeof(_sampleBuffer));
    LOG("TrafficMonitor statistics reset");
}

void TrafficMonitor::pushSample(uint32_t timestamp, uint16_t pulseCount) {
    _sampleBuffer[_sampleHead].timestamp = timestamp;
    _sampleBuffer[_sampleHead].pulseCount = pulseCount;
    _sampleBuffer[_sampleHead].active = (pulseCount > 0);
    
    _sampleHead = (_sampleHead + 1) % MAX_SAMPLES;
    if (_sampleCount < MAX_SAMPLES) {
        _sampleCount++;
    }
}
