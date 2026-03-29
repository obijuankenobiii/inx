#pragma once
#include <cstdint>

class BatteryMonitor {
public:
    // Optional divider multiplier parameter defaults to 2.0
    explicit BatteryMonitor(uint8_t adcPin, float dividerMultiplier = 2.0f);

    // Read voltage and return percentage (0-100)
    uint16_t readPercentage() const;

    // Read the battery voltage in millivolts (accounts for divider)
    uint16_t readMillivolts() const;

    // Read raw millivolts from ADC (doesn't account for divider)
    uint16_t readRawMillivolts() const;

    // Read the battery voltage in volts (accounts for divider)
    double readVolts() const;

    // Percentage (0-100) from a millivolt value
    static uint16_t percentageFromMillivolts(uint16_t millivolts);

    // Calibrate a raw ADC reading and return millivolts
    static uint16_t millivoltsFromRawAdc(uint16_t adc_raw);

private:
    uint8_t _adcPin;
    float _dividerMultiplier;
};
