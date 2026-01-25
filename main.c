/*
 * Main function code for GGA hardware functions: buttons and battery
 */

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <unistd.h>
#include <linux/reboot.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>

#include "battery_gauge.h"
#include "arcade_buttons.h"

// Device file paths
#define I2C_PATH    "/dev/i2c-1"
#define GPIO_PATH   "/dev/gpiochip0"


// I2C addresses
#define BATTERY_GAUGE_ADDR  0x41
#define ARCADE_BONNET_ADDR  0x26

// Other definitions
#define ARCADE_BONNET_INT_PIN   17
#define CONTROLLER_NAME         "GGA Controller"
#define BATTERY_UPDATE_INTERVAL 200
#define BATTERY_SAMPLE_BUFFER   128
#define BATTERY_MIN_VOLTAGE     9.0
#define BATTERY_CAPACITY_MAH    2500
#define BATTERY_SHUTDOWN_LIMIT  0.1
#define BATTERY_OUTPUT_DIR  "/run/bat"

// Key codes needed
const static unsigned int KEYCODES[] = {
    
    KEY_LEFTCTRL,   // SELECT
    KEY_S,          // START
    KEY_ENTER,      // A
    KEY_Y,          // Y
    KEY_ESC,        // B
    KEY_X,          // X

    KEY_9,          // RB
    KEY_2,          // RT
    KEY_1,          // LT
    KEY_8,          // LB

    KEY_UP,
    KEY_DOWN,
    KEY_RIGHT,
    KEY_LEFT,
};    

// Global variables
int verbose = 0, batt_charging_last = -1, batt_percentage_last = -1;
ina219_config* battery_gauge = NULL;
arcade_bonnet* buttons = NULL;
struct libevdev *dev = NULL;
struct libevdev_uinput *uidev = NULL;

// Exit handler
void close_resources()
{
    if (buttons) close_arcade_bonnet(buttons);
    if (battery_gauge) close_ina219(battery_gauge);
    if (uidev) libevdev_uinput_destroy(uidev);
    if (dev) libevdev_free(dev);
}
void exit_handler(int signal)
{
    printf("Exiting GGA...\n");
    close_resources();
    exit(0);
}

// Battery percentage callback function
void battery_handler(double percentage, int charging)
{
    int statusfile, capacityfile, p = (int)round(percentage * 100);
    if (charging != batt_charging_last)
    {
        batt_charging_last = charging;
        statusfile = open(BATTERY_OUTPUT_DIR "/status",
            O_CREAT | O_TRUNC | O_WRONLY,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (statusfile < 0)
        {
            fprintf(stderr,
                "Error: cannot create files in " BATTERY_OUTPUT_DIR "\n");
            close_resources();
            exit(-1);
        }
        dprintf(statusfile, "%s\n", charging ? "Charging" : "Discharging");
        close(statusfile);
    }
    if (p != batt_percentage_last)
    {
        batt_percentage_last = p;
        capacityfile = open(BATTERY_OUTPUT_DIR "/capacity", 
            O_CREAT | O_TRUNC | O_WRONLY,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (capacityfile < 0)
        {
            fprintf(stderr,
                "Error: cannot create files in " BATTERY_OUTPUT_DIR "\n");
            close_resources();
            if (statusfile > 0) close(statusfile);
            exit(-1);
        }
        dprintf(capacityfile, "%d\n", p);
        close(capacityfile);
    }

    if (percentage <= BATTERY_SHUTDOWN_LIMIT && !charging)
    {
        printf("Battery at %d%%, powing down system\n", p);
        reboot(LINUX_REBOOT_CMD_POWER_OFF);
    }
}

// Buttons callback function
void check_press_button(arcade_buttons button, arcade_buttons change,
    arcade_buttons current, unsigned int key)
{
    if (change & button)
    {
        int pressed = (current & button) == 0;
        if (verbose) printf("Button %d state %d\n", button, pressed);
        libevdev_uinput_write_event(uidev, EV_KEY, key, pressed);
    }
}
void button_handler(arcade_buttons last_state, arcade_buttons curr_state,
    struct libevdev_uinput *uidev)
{
    uint16_t changes = last_state ^ curr_state;
    check_press_button(BUTTON_1A,   changes, curr_state, KEYCODES[0]);
    check_press_button(BUTTON_1B,   changes, curr_state, KEYCODES[1]);
    check_press_button(BUTTON_1C,   changes, curr_state, KEYCODES[2]);
    check_press_button(BUTTON_1D,   changes, curr_state, KEYCODES[3]);
    check_press_button(BUTTON_1E,   changes, curr_state, KEYCODES[4]);
    check_press_button(BUTTON_1F,   changes, curr_state, KEYCODES[5]);
    check_press_button(PAD_DOWN,    changes, curr_state, KEYCODES[6]);
    check_press_button(PAD_UP,      changes, curr_state, KEYCODES[7]);
    check_press_button(PAD_RIGHT,   changes, curr_state, KEYCODES[8]);
    check_press_button(PAD_LEFT,    changes, curr_state, KEYCODES[9]);
    check_press_button(STICK_RIGHT, changes, curr_state, KEYCODES[10]);
    check_press_button(STICK_LEFT,  changes, curr_state, KEYCODES[11]); 
    check_press_button(STICK_DOWN,  changes, curr_state, KEYCODES[12]);
    check_press_button(STICK_UP,    changes, curr_state, KEYCODES[13]);
    libevdev_uinput_write_event(uidev, EV_SYN, SYN_REPORT, 0);
} 

int main(int argc, char** argv)
{
    struct timespec current_ts, last_ts;
    arcade_buttons last_state;
    int enable_buttons = 1, enable_battery = 1;
    double battery_current_history[BATTERY_SAMPLE_BUFFER];
    double last_capacity;

    // Handle flags
    while (argc > 1)
    {
        if (argv[argc - 1][0] == '-')
        {
            switch (argv[argc - 1][1])
            {
                case 'b':
                    enable_battery = 0;
                    break;
                case 's':
                    enable_buttons = 0;
                    break;
                case 'v':
                    verbose = 1;
                    break;
                case 'h':
                    printf("GGA: hardware handler for GGA console.\n"
                        "  -h Display this help text\n"
                        "  -v Increase verbosity\n"
                        "  -b Don't enable battery monitoring\n"
                        "  -s Don't enable buttons monitoring\n");
                    return 0;
            }
        }
        argc--;
    }
  
    if (enable_buttons)
    {
        // Set up keyboard input device
        dev = libevdev_new();
        libevdev_set_name(dev, CONTROLLER_NAME);
        libevdev_enable_event_type(dev, EV_KEY);
        for (int i = 0; i < ARCADE_BUTTONS_COUNT; i++)
        {
            libevdev_enable_event_code(dev, EV_KEY, KEYCODES[i], NULL);
        }
        if (libevdev_uinput_create_from_device(
            dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev) != 0)
        {
            fprintf(stderr, "Error: cannot create keyboard device!\n");
            close_resources();
            return -1;
        }
        // Initialize arcade bonnet
        buttons = configure_arcade_bonnet(ARCADE_BONNET_ADDR, I2C_PATH);
        if (!buttons)
        {
            fprintf(stderr, "Error: cannot setup arcade bonnet IC!\n");
            close_resources();
            return -1;
        }
        last_state = buttons->state;
        #ifdef GPIO_INT
        // Setup GPIO interrupt
        configure_button_interrupt(GPIO_PATH, ARCADE_BONNET_INT_PIN, buttons);
        #endif
    }

    if (enable_battery)
    {
        // Setup battery logging files directory
        if (mkdir(BATTERY_OUTPUT_DIR,
            S_ISVTX | S_IRWXU | S_IWGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0)
        {
            if (errno != EEXIST)
            {
                fprintf(stderr, "Error: cannot create "BATTERY_OUTPUT_DIR"\n");
                close_resources();
                return -1;
            }
            else if (chmod(BATTERY_OUTPUT_DIR,
                S_ISVTX | S_IRWXU | S_IWGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0)
            {
                fprintf(stderr, "Error: already exits "BATTERY_OUTPUT_DIR"\n");
                close_resources();
                return -1;
            }
        }
        // Initialize battery gauge
        battery_gauge = initialize_ina219(
            BATTERY_GAUGE_ADDR, I2C_PATH, BUS_VOLTAGE_RANGE_16V_5A);
        if (!battery_gauge)
        {
            fprintf(stderr, "Error: cannot setup battery gauge IC!\n");
            close_resources();
            return -1;
        }
        // Set up battery monitoring
        memset(battery_current_history, 0,
            BATTERY_SAMPLE_BUFFER * sizeof(double));
        last_capacity = estimate_battery_percentage(
            BATTERY_MIN_VOLTAGE, battery_gauge) * BATTERY_CAPACITY_MAH;
        clock_gettime(CLOCK_REALTIME, &last_ts);
    }

    // Set up interrupt handlers
    signal(SIGTERM, exit_handler);
    signal(SIGINT, exit_handler);
    signal(SIGQUIT, exit_handler);

    printf("Started GGA\n");

    for (;;)
    {
        unsigned int ms_passed = 0;
        if (enable_buttons)
        {
            #ifdef GPIO_INT
            int button_update = wait_for_button_interrupt(
                buttons, BATTERY_UPDATE_INTERVAL);
            #else
            usleep(10000); // 10ms
            int button_update = read_buttons_pressed(buttons);
            #endif

            if (button_update)
            {
                button_handler(last_state, buttons->state, uidev);
                last_state = buttons->state;
            }
        }
        if (enable_battery)
        {
            clock_gettime(CLOCK_REALTIME, &current_ts);
            ms_passed = ((current_ts.tv_sec - last_ts.tv_sec) * 1000)
                + ((current_ts.tv_nsec - last_ts.tv_nsec) / 1000000);
            if (ms_passed >= BATTERY_UPDATE_INTERVAL)
            {
                double shunt_voltage = get_shunt_voltage(battery_gauge);
                double current = get_current(battery_gauge);
                double new_capacity = last_capacity + 
                    (current * ((double)ms_passed / 3.6e6));
                int charging = 0;
                
                battery_current_history[0] = current;
                for (int i = BATTERY_SAMPLE_BUFFER - 1; i > 0; i--)
                {
                    battery_current_history[i] = battery_current_history[i - 1];
                    if (battery_current_history[i] > 0)
                    {
                        charging = 1;
                    }
                }
                last_capacity = new_capacity;
                last_ts = current_ts;
                battery_handler(new_capacity / BATTERY_CAPACITY_MAH, charging);
                
                if (verbose)
                {
                    printf("Battery: %lf%% (%s), %lf V, %lf mA, %lf mAh\n",
                        100 * (new_capacity / BATTERY_CAPACITY_MAH),
                        charging ? "Charging" : "Discharging",
                        get_bus_voltage(battery_gauge),
                        current, new_capacity);
                }
            }
        }
    }
    close_resources();
    return 0;
} 
