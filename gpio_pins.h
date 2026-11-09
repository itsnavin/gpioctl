/*
 * gpio_pins.h - Raspberry Pi 5 40-pin header pin definitions for gpioctl.
 *
 * Physical header pin numbers (1-40) -> BCM GPIO numbers, power/ground
 * pins, and a full per-pin info table (type + default alternate function).
 */
#ifndef GPIO_PINS_H
#define GPIO_PINS_H

#include <stddef.h>

#define HEADER_PINS 40

/* Physical header pin -> BCM GPIO number (GPIO-capable pins only) */
static const struct { int pin, bcm; } PIN_MAP[] = {
    {3, 2},   {5, 3},   {7, 4},   {8, 14},  {10, 15}, {11, 17}, {12, 18},
    {13, 27}, {15, 22}, {16, 23}, {18, 24}, {19, 10}, {21, 9},  {22, 25},
    {23, 11}, {24, 8},  {26, 7},  {27, 0},  {28, 1},  {29, 5},  {31, 6},
    {32, 12}, {33, 13}, {35, 19}, {36, 16}, {37, 26}, {38, 20}, {40, 21},
};
#define PIN_MAP_LEN (sizeof(PIN_MAP) / sizeof(PIN_MAP[0]))

/* Power and ground pins */
static const struct { int pin; const char *name; } POWER_GND[] = {
    {1, "3.3V"}, {17, "3.3V"}, {2, "5V"},   {4, "5V"},
    {6, "GND"},  {9, "GND"},   {14, "GND"}, {20, "GND"},
    {25, "GND"}, {30, "GND"},  {34, "GND"}, {39, "GND"},
};
#define POWER_LEN (sizeof(POWER_GND) / sizeof(POWER_GND[0]))

/*
 * Full 40-pin header description.
 *   pin  : physical header pin (1-40)
 *   bcm  : BCM GPIO number, or -1 for power/ground
 *   type : "GPIO", "3.3V", "5V" or "GND"
 *   func : common alternate function ("" for plain GPIO / power pins)
 */
struct pin_info {
    int pin;
    int bcm;
    const char *type;
    const char *func;
};

static const struct pin_info PIN_INFO[] = {
    { 1, -1, "3.3V", "Power"           },
    { 2, -1, "5V",   "Power"           },
    { 3,  2, "GPIO", "I2C1 SDA"        },
    { 4, -1, "5V",   "Power"           },
    { 5,  3, "GPIO", "I2C1 SCL"        },
    { 6, -1, "GND",  "Ground"          },
    { 7,  4, "GPIO", "GPCLK0"          },
    { 8, 14, "GPIO", "UART0 TX"        },
    { 9, -1, "GND",  "Ground"          },
    {10, 15, "GPIO", "UART0 RX"        },
    {11, 17, "GPIO", ""                },
    {12, 18, "GPIO", "PWM0 / PCM CLK"  },
    {13, 27, "GPIO", ""                },
    {14, -1, "GND",  "Ground"          },
    {15, 22, "GPIO", ""                },
    {16, 23, "GPIO", ""                },
    {17, -1, "3.3V", "Power"           },
    {18, 24, "GPIO", ""                },
    {19, 10, "GPIO", "SPI0 MOSI"       },
    {20, -1, "GND",  "Ground"          },
    {21,  9, "GPIO", "SPI0 MISO"       },
    {22, 25, "GPIO", ""                },
    {23, 11, "GPIO", "SPI0 SCLK"       },
    {24,  8, "GPIO", "SPI0 CE0"        },
    {25, -1, "GND",  "Ground"          },
    {26,  7, "GPIO", "SPI0 CE1"        },
    {27,  0, "GPIO", "ID_SD (EEPROM)"  },
    {28,  1, "GPIO", "ID_SC (EEPROM)"  },
    {29,  5, "GPIO", ""                },
    {30, -1, "GND",  "Ground"          },
    {31,  6, "GPIO", ""                },
    {32, 12, "GPIO", "PWM0"            },
    {33, 13, "GPIO", "PWM1"            },
    {34, -1, "GND",  "Ground"          },
    {35, 19, "GPIO", "PWM1 / PCM FS"   },
    {36, 16, "GPIO", "SPI1 CE2"        },
    {37, 26, "GPIO", ""                },
    {38, 20, "GPIO", "PCM DIN"         },
    {39, -1, "GND",  "Ground"          },
    {40, 21, "GPIO", "PCM DOUT"        },
};
#define PIN_INFO_LEN (sizeof(PIN_INFO) / sizeof(PIN_INFO[0]))

#endif /* GPIO_PINS_H */
