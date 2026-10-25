// Minimal LittleFS helpers, derived from the arduino-esp32 LITTLEFS_test
// example: https://github.com/espressif/arduino-esp32
#pragma once

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>

// Format the filesystem automatically when it cannot be mounted.
#define FORMAT_LITTLEFS_IF_FAILED true

// Recursively list a directory over Serial.
inline void listDir(fs::FS &fs, const char *dirname, uint8_t levels) {
    Serial.printf("Listing directory: %s\r\n", dirname);

    File root = fs.open(dirname);
    if (!root) {
        Serial.println("- failed to open directory");
        return;
    }
    if (!root.isDirectory()) {
        Serial.println(" - not a directory");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if (levels) {
                listDir(fs, file.path(), levels - 1);
            }
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}
