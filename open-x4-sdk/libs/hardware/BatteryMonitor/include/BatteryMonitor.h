#pragma once

/**
 * @file BatteryMonitor.h
 * @brief Public interface and types for BatteryMonitor.
 */

#include <cstdint>

class BatteryMonitor {
public:
    
    explicit BatteryMonitor(uint8_t adcPin, float dividerMultiplier = 2.0f);

    
    uint16_t readPercentage() const;

    
    uint16_t readMillivolts() const;

    
    uint16_t readRawMillivolts() const;

    
    double readVolts() const;

    
    static uint16_t percentageFromMillivolts(uint16_t millivolts);

    
    static uint16_t millivoltsFromRawAdc(uint16_t adc_raw);

private:
    uint8_t _adcPin;
    float _dividerMultiplier;
};
