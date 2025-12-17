/*
 * Implements an interface for the AdaFruit Arcade bonnet with a MCP23017
 */

#ifndef ARCADE_BUTTONS_H
#define ARCADE_BUTTONS_H

#include <stdint.h>
#include <unistd.h>

#ifdef GPIO_INT
#include <gpiod.h>
#endif

#define ARCADE_BUTTONS_COUNT 14

typedef enum {
    BUTTON_1A   = 0x0001,
    BUTTON_1B   = 0x0002,
    BUTTON_1C   = 0x0004,
    BUTTON_1D   = 0x0008,
    BUTTON_1E   = 0x0010,
    BUTTON_1F   = 0x0020,
    PAD_DOWN    = 0x0100,
    PAD_UP      = 0x0200,
    PAD_RIGHT   = 0x0400,
    PAD_LEFT    = 0x0800,
    STICK_RIGHT = 0x1000,
    STICK_LEFT  = 0x2000,
    STICK_DOWN  = 0x4000,
    STICK_UP    = 0x8000,
} arcade_buttons;

typedef struct {
    int i2c_bus;
    arcade_buttons state;
#ifdef GPIO_INT
	struct gpiod_line_request* int_pin;
    struct gpiod_edge_event_buffer* events;
#else
    void* int_pin;
#endif
} arcade_bonnet;

/*
 * Sets initial MCP23017 config and returns a configuration struct
 */
arcade_bonnet* configure_arcade_bonnet(long addr, char* bus);

/*
 * Updates the current button state, and returns 1 if there are changes from the
 * last update, -1 on read error, otherwise 0
 */
int read_buttons_pressed(arcade_bonnet* bonnet);

#ifdef GPIO_INT
/*
 * Configures a pin change interrupt on button value changes, returns negative
 * number on error, zero on success, and a negative value on errors
 */
int configure_button_interrupt(
    const char* gpiochip, unsigned int pin, arcade_bonnet* bonnet);

/*
 * Waits for a button press interrupt, then reads the new button and immediatly
 * returns 1. If there are no events in `ms` milliseconds, returns 0
 */
int wait_for_button_interrupt(arcade_bonnet* bonnet, int ms);
#endif

/*
 * Closes communications to arcade bonnet and frees memory
 */
void close_arcade_bonnet(arcade_bonnet* bonnet);

#endif
