/*
 * Implements an interface for communicating with INA219 battery gauge module.
 */

#ifndef BATTERY_GAUGE_H
#define BATTERY_GAUGE_H

#include <unistd.h>
#include <stdint.h>

// Bus voltage range values
#define BUS_VOLTAGE_RANGE_16V_5A   0x00
#define BUS_VOLTAGE_RANGE_23V_2A   0x01

// Gain values
#define GAIN_DIV_1_40MV     0x00
#define GAIN_DIV_2_80MV     0x01
#define GAIN_DIV_4_160MV    0x02
#define GAIN_DIV_8_320MV    0x03

/*
 * Holds configuration info for a INA219 battery gauge chip
 */
typedef struct
{
    int i2c_bus;
    uint16_t cal_value;
    uint16_t bus_voltage;
    uint16_t gain;
    double current_lsb;
    double power_lsb;
} ina219_config;

/*
 * Sends configuration and calibration values to INA219 chip
 */
int configure_ina219(ina219_config* chip);

/*
 * Allocates a `ina219_config` and sets good default values for a given voltage
 * range
 */
ina219_config* initialize_ina219(
    long addr, char* bus, uint8_t bus_voltage_range);

/*
 * Queries and returns current shunt voltage value in volts
 */
double get_shunt_voltage(ina219_config* chip);

/*
 * Queries and returns current bus voltage value in volts
 */
double get_bus_voltage(ina219_config* chip);

/*
 * Queries and returns current in mA
 */
double get_current(ina219_config* chip);

/*
 * Queries and returns current power usage in watts
 */
double get_power(ina219_config* chip);

/*
 * Approximates battery percentage based on current bus voltage
 */
double estimate_battery_percentage(
    double battery_min_volts, ina219_config* chip);

/*
 * Closes INA219 bus and deallocates config structure
 */
void close_ina219(ina219_config* chip);

#endif
