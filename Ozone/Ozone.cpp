/*
 * Copyright 2024 Reef Angel / Brennyn Cutler
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Ozone.h"
#include "Globals.h" // This should define DATA_REGISTER, MODE_REGISTER, and i2cOzone
#include <Wire.h>

// Constructor implementation
OzoneClass::OzoneClass() : OzoneLevel(0) {}

// Initialize the sensor
bool OzoneClass::begin() {
    Wire.begin(); // Initialize I2C communication
    Wire.beginTransmission(i2cOzone);
    if (Wire.endTransmission() == 0) {
        setActiveMode(); // Configure sensor to start measurements
        return true;
    }
    int OzoneLevel = 0;
    return false; // Sensor not found or not responsive
}

// Configure the sensor to automatically measure ozone levels
void OzoneClass::setActiveMode() {
    writeRegister(MODE_REGISTER, MEASURE_MODE_AUTOMATIC);
}

// Read and average the ozone level
int OzoneClass::readOzoneLevel() {
    
    const uint8_t numReadings = 20;
    long sum = 0;
    int validReadings = 0;
    
    for (uint8_t i = 0; i < numReadings; ++i) {
        int reading = i2cReadOzoneData();
        if (reading >= 0) { // Validate reading
            sum += reading;
            ++validReadings;
        }
        
    }

    if (validReadings > 0) {
        OzoneLevel = sum / validReadings;
    } else {
        OzoneLevel = -1; // Indicate error or no valid readings
    }
    return OzoneLevel;
}

// Helper function to write to a sensor register
void OzoneClass::writeRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(i2cOzone);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

// Read ozone data from the sensor
int OzoneClass::i2cReadOzoneData() {
    Wire.beginTransmission(i2cOzone);
    Wire.write(DATA_REGISTER); // Specify the data register
    Wire.endTransmission(false); // Keep the connection open for reading

    Wire.requestFrom(i2cOzone, 2); // Request 2 bytes of data
    if (Wire.available() < 2) {
        return -1; // Error if data is not available
    }

    uint8_t msb = Wire.read(); // Most significant byte
    uint8_t lsb = Wire.read(); // Least significant byte
    return (msb << 8) | lsb; // Combine bytes and return the result
}