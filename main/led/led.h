#pragma once

#include <stdint.h>

typedef enum LEDEffect_t {
    LED_OFF = 0,
    LED_SOLID,
    LED_BLINK,
    LED_BREATHE,
    LED_CYCLIC,
    LED_RAINBOW,
} LEDEffect_t;

void led_set_effect(LEDEffect_t effect);
void led_set_color(uint8_t r, uint8_t g, uint8_t b);
void led_set_speed(uint8_t speed);
void led_set_brightness(uint8_t brightness);
void led_fade_out();
void led_fade_in();
void led_init();