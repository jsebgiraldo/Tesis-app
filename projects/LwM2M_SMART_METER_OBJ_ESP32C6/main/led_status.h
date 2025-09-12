// LED status driver for a single WS2812 (NeoPixel) RGB LED
#pragma once
#include <stdint.h>

typedef enum {
    LED_MODE_OFF = 0,
    LED_MODE_FACTORY_RESET,   // red fast blink
    LED_MODE_PROV_BLE,        // blue breathing
    LED_MODE_WIFI_CONNECTED,  // solid green
    LED_MODE_WIFI_FAIL,       // amber blink (red+green)
} led_mode_t;

void led_status_init(void);
void led_status_set_mode(led_mode_t mode);
void led_status_get_color(uint8_t* r, uint8_t* g, uint8_t* b);
// Immediately switch off the LED and set mode to OFF
void led_status_force_off(void);
