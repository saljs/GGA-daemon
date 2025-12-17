/*
 * Implements an interface for communicating with INA219 battery gauge module.
 */

#include <stdlib.h>

// Needed for i2c bus
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>

#include "battery_gauge.h"

// Register values
#define REG_CONFIG          0x00
#define REG_SHUNTVOLTAGE    0x01
#define REG_BUSVOLTAGE      0x02
#define REG_POWER           0x03
#define REG_CURRENT         0x04
#define REG_CALIBRATION     0x05

// Configuration values
#define INA219_MODE             0x07
#define BUS_ADC_RESOLUTION      0x0D
#define SHUNT_ADC_RESOLUTION    0x0D

/*
 * Private helper functions
 */
int i2c_write_word(int bus, uint8_t reg, uint16_t value)
{
    struct i2c_smbus_ioctl_data args;
    union i2c_smbus_data data;
    int err;
    args.read_write = I2C_SMBUS_WRITE;
    args.command = reg;
    args.size = I2C_SMBUS_WORD_DATA;
    args.data = &data;
    data.word = ((value & 0x00FF) << 8) | ((value & 0xFF00) >> 8);

    if (ioctl(bus, I2C_SMBUS, &args) < 0)
    {
        return -1;
    }
    return 0;
}
int i2c_read_word(int bus, uint8_t reg)
{
    struct i2c_smbus_ioctl_data args;
    union i2c_smbus_data data;
    int err;
    args.read_write = I2C_SMBUS_READ;
    args.command = reg;
    args.size = I2C_SMBUS_WORD_DATA;
    args.data = &data;

    if (ioctl(bus, I2C_SMBUS, &args) < 0)
    {
        return -1;
    }
    return ((data.word & 0x00FF) << 8) | ((data.word & 0xFF00) >> 8);
}

/*
 * Sends configuration and calibration values to INA219 chip
 */
int configure_ina219(ina219_config* chip)
{
    uint16_t config_data = 0;
    // Set calibration register
    if (i2c_write_word(chip->i2c_bus, REG_CALIBRATION, chip->cal_value) < 0)
    {
        return -1;
    }

    // Set config register
    config_data = (chip->bus_voltage << 13)
        | (chip->gain << 11)
        | (BUS_ADC_RESOLUTION << 7)
        | (SHUNT_ADC_RESOLUTION << 3)
        | INA219_MODE;
    if (i2c_write_word(chip->i2c_bus, REG_CONFIG, config_data) < 0)
    {
        return -2;
    }
    return 0;
}


/*
 * Allocates a `ina219_config` and sets good default values for a given voltage
 * range, as well as setting up the chip
 */
ina219_config* initialize_ina219(long addr, char* bus, uint8_t bus_voltage_range)
{
    ina219_config* chip = malloc(sizeof(ina219_config));
    if (chip == NULL)
    {
        return NULL;
    }

    if (bus_voltage_range == BUS_VOLTAGE_RANGE_23V_2A)
    {
        chip->bus_voltage = BUS_VOLTAGE_RANGE_23V_2A;
        chip->gain = GAIN_DIV_8_320MV;
        chip->cal_value = 4096;
        chip->current_lsb = 0.1;    // Current LSB 100uA per bit
        chip->power_lsb = 0.002;    // Power LSB 2mW per bit
    }
    else if (bus_voltage_range == BUS_VOLTAGE_RANGE_16V_5A)
    {
        chip->bus_voltage = BUS_VOLTAGE_RANGE_16V_5A;
        chip->gain = GAIN_DIV_2_80MV;
        chip->cal_value = 26868;
        chip->current_lsb = 0.1524;    // Current LSB 100uA per bit
        chip->power_lsb = 0.003048;    // Power LSB 2mW per bit
    }
    else
    {
        // Voltage range not implemented
        return NULL;
    }

    chip->i2c_bus = open(bus, O_RDWR);
    if (chip->i2c_bus < 0)
    {
        return NULL;
    }
    else if (ioctl(chip->i2c_bus, I2C_SLAVE, addr))
    {
        close_ina219(chip);
        return NULL;
    }

    if (configure_ina219(chip) < 0)
    {
        // Chip setup failed
        close_ina219(chip);
        return NULL;
    }

    return chip;
}

/*
 * Queries and returns current shunt voltage value in volts
 */
double get_shunt_voltage(ina219_config* chip)
{
    int val;
    // Write calibration register
    if (i2c_write_word(chip->i2c_bus, REG_CALIBRATION, chip->cal_value) < 0)
    {
        return -255.0;
    }

    // Read voltage value
    val = i2c_read_word(chip->i2c_bus, REG_SHUNTVOLTAGE);
    if (val > 32767) val -= 65535;
    return val * 0.00001;
}

/*
 * Queries and returns current bus voltage value in volts
 */
double get_bus_voltage(ina219_config* chip)
{
    int val;
    // Write calibration register
    if (i2c_write_word(chip->i2c_bus, REG_CALIBRATION, chip->cal_value) < 0)
    {
        return -255.0;
    }

    // Read voltage value
    val = i2c_read_word(chip->i2c_bus, REG_BUSVOLTAGE);
    return (val >> 3) * 0.004;
}

/*
 * Queries and returns current in mA
 */
double get_current(ina219_config* chip)
{
    int val;
    // Write calibration register
    if (i2c_write_word(chip->i2c_bus, REG_CALIBRATION, chip->cal_value) < 0)
    {
        return -255.0;
    }

    // Read current value
    val = i2c_read_word(chip->i2c_bus, REG_CURRENT);
    if (val > 32767) val -= 65535;
    return val * chip->current_lsb;
}

/*
 * Queries and returns current power usage in watts
 */
double get_power(ina219_config* chip)
{
    int val;
    // Write calibration register
    if (i2c_write_word(chip->i2c_bus, REG_CALIBRATION, chip->cal_value) < 0)
    {
        return -255.0;
    }

    // Read power value
    val = i2c_read_word(chip->i2c_bus, REG_POWER);
    if (val > 32767) val -= 65535;
    return val * chip->power_lsb;
}

/*
 * Approximates battery percentage based on current bus voltage
 */
double estimate_battery_percentage(
    double battery_min_volts, ina219_config* chip)
{
    // Linear approximation of Li-ion voltage curve
    double batt = (get_bus_voltage(chip) - battery_min_volts) / 3.6;
    if (batt > 1) batt = 1.0;
    else if (batt < 0) batt = 0.0;
    return batt;
}

/*
 * Closes INA219 bus and deallocates config structure
 */
void close_ina219(ina219_config* chip)
{
    close(chip->i2c_bus);
    free(chip);
}
