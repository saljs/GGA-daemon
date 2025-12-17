/*
 * Implements an interface for the AdaFruit Arcade bonnet with a MCP23017
 */

#include <stdlib.h>

// Needed for i2c bus
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#include "arcade_buttons.h"

// Register values
#define IODIRA  0x00
#define IOCONA  0x0A
#define INTCAPA 0x10

#define CONSUMER_NAME "arcade-bonnet"
#define EVENT_BUFFER_LEN 64

/*
 * Sets initial MCP23017 config and returns a configuration struct
 */
arcade_bonnet* configure_arcade_bonnet(long addr, char* bus)
{
    uint8_t buf[16];
    arcade_bonnet* bonnet = malloc(sizeof(arcade_bonnet));
    bonnet->int_pin = NULL;
    
    // Open bus
    bonnet->i2c_bus = open(bus, O_RDWR);
    if (bonnet->i2c_bus < 0)
    {
        return NULL;
    }
    else if (ioctl(bonnet->i2c_bus, I2C_SLAVE, addr))
    {
        close_arcade_bonnet(bonnet);
        return NULL;
    }
    
    // If bank 1, switch to 0
    buf[0] = 0x05;
    buf[1] = 0x00;
    if (write(bonnet->i2c_bus, buf, 2) != 2)
    {
        close_arcade_bonnet(bonnet);
        return NULL;
    }

    // Bank 0, INTB=A, seq, OD IRQ
    buf[0] = IOCONA;
    buf[1] = 0x44;
    if (write(bonnet->i2c_bus, buf, 2) != 2)
    {
        close_arcade_bonnet(bonnet);
        return NULL;
    }

    // Read in config values
    buf[0] = IODIRA;
    if (write(bonnet->i2c_bus, buf, 1) != 1)
    {
        close_arcade_bonnet(bonnet);
        return NULL;
    }
    if (read(bonnet->i2c_bus, buf + 1, 14) != 14)
    {
        close_arcade_bonnet(bonnet);
        return NULL;
    }

    // Set input bits
    buf[0x1] = 0xFF; buf[0x2] = 0xFF;
    // Set polarity
    buf[0x3] = 0x00; buf[0x4] = 0x00;
    // Interrupt pins
    buf[0x5] = 0xFF; buf[0x6] = 0xFF;
    // Pull-ups
    buf[0xD] = 0xFF; buf[0xE] = 0xFF;
    // Write to register
    if (write(bonnet->i2c_bus, buf, 15) != 15) {
        close_arcade_bonnet(bonnet);
        return NULL;
    }

    // Clear interrupt with read
    read_buttons_pressed(bonnet);
    return bonnet;
}

/*
 * Updates the current button state, and returns 1 if there are changes from the
 * last update, -1 on read error, otherwise 0
 */
int read_buttons_pressed(arcade_bonnet* bonnet)
{
    uint8_t buf[5];
    uint16_t val;
    arcade_buttons old_state = bonnet->state;

    // Write register values
    buf[0] = INTCAPA;
    if (write(bonnet->i2c_bus, buf, 1) != 1)
    {
        return -1;
    }
    
    if (read(bonnet->i2c_bus, buf, 4) != 4)
    {
        return -1;
    }

    val = buf[2] | (buf[3] << 8);
    bonnet->state = (arcade_buttons)val;
    return bonnet->state != old_state;
}

#ifdef GPIO_INT
/*
 * Configures a pin change interrupt on button value changes, returns negative
 * number on error, zero on success, and a negative value on errors
 */
int configure_button_interrupt(
    const char* gpiochip, unsigned int pin, arcade_bonnet* bonnet)
{
    struct gpiod_line_request *request = NULL;
    struct gpiod_chip* gpio = gpiod_chip_open(gpiochip);
    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    struct gpiod_line_config *line_cfg = gpiod_line_config_new();
    struct gpiod_request_config *req_cfg = gpiod_request_config_new();
    if (!gpio)
    {
        return -1;
    }
    else if (!settings || !line_cfg || !req_cfg)
    {
        if (settings) gpiod_line_settings_free(settings);
        if (line_cfg) gpiod_line_config_free(line_cfg);
        if (req_cfg) gpiod_request_config_free(req_cfg);
        gpiod_chip_close(gpio);
        return -2;
    }
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_FALLING);
    gpiod_request_config_set_consumer(req_cfg, CONSUMER_NAME);
    if(gpiod_line_config_add_line_settings(line_cfg, &pin, 1, settings) != 0)
    {
        gpiod_line_settings_free(settings);
        gpiod_line_config_free(line_cfg);
        gpiod_request_config_free(req_cfg);
        gpiod_chip_close(gpio);
        return -3;
        
    }
    bonnet->int_pin = gpiod_chip_request_lines(gpio, req_cfg, line_cfg);
    bonnet->events = gpiod_edge_event_buffer_new(EVENT_BUFFER_LEN);
    gpiod_line_settings_free(settings);
    gpiod_line_config_free(line_cfg);
    gpiod_request_config_free(req_cfg);
    gpiod_chip_close(gpio);
    if (!bonnet->int_pin || !bonnet->events)
    {
        return -4;
    }

    return 0;
}

/*
 * Waits for a button press interrupt, then reads the new button and immediatly
 * returns 1. If there are no events in `ms` milliseconds, returns 0
 */
int wait_for_button_interrupt(arcade_bonnet* bonnet, int ms)
{
    int ret = gpiod_line_request_wait_edge_events(
        bonnet->int_pin, ms * 1000000);
    if (ret > 0)
    {
        // Read event(s) from line to clear it
        gpiod_edge_event_buffer_get_event(bonnet->events, ret);
        return read_buttons_pressed(bonnet);
    }
    return ret;
}
#endif

/*
 * Closes communications to arcade bonnet and frees memory
 */
void close_arcade_bonnet(arcade_bonnet* bonnet)
{
    close(bonnet->i2c_bus);
    if (bonnet->int_pin)
    {
        #ifdef GPIO_INT
        gpiod_line_request_release(bonnet->int_pin);
        gpiod_edge_event_buffer_free(bonnet->events);
        #endif
    }
    free(bonnet);
}
