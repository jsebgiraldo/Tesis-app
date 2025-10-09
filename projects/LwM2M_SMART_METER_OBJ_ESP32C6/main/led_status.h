// LED module removed intentionally. Keep minimal stub to satisfy includes.
#pragma once
typedef enum { LED_MODE_OFF = 0 } led_mode_t;
static inline void led_status_init(void) {}
static inline void led_status_set_mode(led_mode_t mode) { (void) mode; }
static inline void led_status_get_color(unsigned char* r, unsigned char* g, unsigned char* b) { if (r) *r=0; if (g) *g=0; if (b) *b=0; }
static inline void led_status_force_off(void) {}
