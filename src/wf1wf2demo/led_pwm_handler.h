#pragma once

#include <Arduino.h>
#include <driver/ledc.h>
#include "esp_err.h"

// LED control settings
const int LED_RESOLUTION = 13;
const unsigned long LED_FADE_TIME = 1000; // ms for a full fade in or out

// Breathing effect settings
const int BREATH_MIN = 0;
const int BREATH_MAX = 255;

// For pointless led fading in and out to tell me it's alive.
//
// The duty range is computed once and the brightness is driven from elapsed
// time, so a full fade takes exactly LED_FADE_TIME regardless of the PWM
// resolution (the old loop produced ~765 ms because of integer division).
inline void ledFadeTask(void *parameter) {
    (void)parameter;

    // Precompute the maximum duty for the configured resolution.
    const uint32_t maxDuty = (1UL << LED_RESOLUTION) - 1UL;

    while (true) {
        // direction 0 = fade in, 1 = fade out
        for (int direction = 0; direction < 2; direction++) {
            const unsigned long startMs = millis();
            unsigned long elapsed = 0;

            do {
                elapsed = millis() - startMs;

                float progress = (float)elapsed / (float)LED_FADE_TIME;
                if (progress > 1.0f) {
                    progress = 1.0f;
                }

                const float level = (direction == 0) ? progress : (1.0f - progress);
                const uint32_t duty = (uint32_t)(level * (float)maxDuty);

                ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty));
                ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));

                delay(5);
            } while (elapsed < LED_FADE_TIME);
        }
    }
}
