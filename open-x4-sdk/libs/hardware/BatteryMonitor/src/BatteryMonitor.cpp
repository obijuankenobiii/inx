#include "BatteryMonitor.h"
#include <esp32-hal-adc.h>
#include <esp_adc_cal.h>

inline float min(const float a, const float b) { return a < b ? a : b; }
inline float max(const float a, const float b) { return a > b ? a : b; }

BatteryMonitor::BatteryMonitor(uint8_t adcPin, float dividerMultiplier)
  : _adcPin(adcPin), _dividerMultiplier(dividerMultiplier)
{
}

uint16_t BatteryMonitor::readPercentage() const
{
    return percentageFromMillivolts(readMillivolts());
}

uint16_t BatteryMonitor::readMillivolts() const
{
    const uint16_t raw = readRawMillivolts();
    const uint32_t mv = millivoltsFromRawAdc(raw);
    return static_cast<uint32_t>(mv * _dividerMultiplier);
}

uint16_t BatteryMonitor::readRawMillivolts() const
{
    const uint16_t raw = analogRead(_adcPin);
    return raw;
}

double BatteryMonitor::readVolts() const
{
    return static_cast<double>(readMillivolts()) / 1000.0;
}

uint16_t BatteryMonitor::percentageFromMillivolts(uint16_t millivolts)
{
    double volts = millivolts / 1000.0;
    // Polynomial derived from LiPo samples
    double y = -144.9390 * volts * volts * volts +
               1655.8629 * volts * volts -
               6158.8520 * volts +
               7501.3202;

    // Clamp to [0,100] and round
    y = max(y, 0.0);
    y = min(y, 100.0);
    y = round(y);
    return static_cast<int>(y);
}

uint16_t BatteryMonitor::millivoltsFromRawAdc(uint16_t adc_raw)
{
    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 1100, &adc_chars);
    return esp_adc_cal_raw_to_voltage(adc_raw, &adc_chars);
}
