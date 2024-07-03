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

#include "Co2.h"
#include "Globals.h" // Include if necessary for global definitions

Co2Sensor::Co2Sensor() {

}

bool Co2Sensor::begin(bool startMeasurements) {
    unsigned long startTime = millis(); // Record the start time
    
    bool success = true;

    // Stop periodic measurement
    success &= writeCommand(SCD4x_COMMAND_STOP_PERIODIC_MEASUREMENT);

    // Wait for 500 milliseconds
    while (millis() - startTime < 500) {
        // Do nothing, just wait
    }

    // Enable automatic self-calibration
    success &= writeCommand(SCD4x_COMMAND_SET_AUTOMATIC_SELF_CALIBRATION_ENABLED);

    // Wait for 500 milliseconds
    startTime = millis(); // Reset the start time
    while (millis() - startTime < 500) {
        // Do nothing, just wait
    }

    // Start periodic measurements if requested
    if (startMeasurements) {
        success &= writeCommand(SCD4x_COMMAND_START_PERIODIC_MEASUREMENT);

        // Wait for 500 milliseconds
        startTime = millis(); // Reset the start time
        while (millis() - startTime < 500) {
            // Do nothing, just wait
        }
    }

    if (success) {
        // Serial.println(F("Initialization successful."));
    } else {
        // Serial.println(F("Initialization failed."));
    }

    return success; // Return the success status.
}


bool Co2Sensor::writeCommand(uint16_t command) {
    Wire.beginTransmission(I2CCo2);
    Wire.write(highByte(command)); // MSB
    Wire.write(lowByte(command));  // LSB
    byte result = Wire.endTransmission();
    return result == 0; // Success if endTransmission returns 0
}

bool Co2Sensor::getDataReadyStatus() {
    uint16_t response;
    bool success = readRegister(SCD4x_COMMAND_GET_DATA_READY_STATUS, &response, 1);

    if (!success) {
      //  Serial.println(F("Failed to get data ready status."));
        return false;
    }

    // Check if the least significant 11 bits of the response are not all zeros
    return (response & 0x07FF) != 0;
}
bool Co2Sensor::readRegister(uint16_t registerAddress, uint16_t* response, uint16_t delayMillis) {
    unsigned long startTime = millis(); // Record the start time

    Wire.beginTransmission(I2CCo2);
    Wire.write(registerAddress >> 8);   // MSB
    Wire.write(registerAddress & 0xFF); // LSB
    if (Wire.endTransmission() != 0) {
        resetValues();
        return false; // Sensor did not ACK
    }

    // Check if the specified delay has passed
    if (millis() - startTime < delayMillis) {
        // Still waiting for the specified delay
        return false;
    }

    Wire.requestFrom(static_cast<uint8_t>(I2CCo2), static_cast<uint8_t>(3)); // Request data and CRC
    if (Wire.available()) {
        uint8_t data[2];
        data[0] = Wire.read();
        data[1] = Wire.read();
        uint8_t crc = Wire.read();
        *response = (static_cast<uint16_t>(data[0]) << 8) | data[1];
        uint8_t expectedCRC = computeCRC8(data, 2);
        if (crc == expectedCRC) {
            return true; // CRC check passed
        } else {
            // Serial.print(F("CRC check failed: expected 0x"));
            // Serial.print(expectedCRC, HEX);
            // Serial.print(F(", got 0x"));
            // Serial.println(crc, HEX);
            return false; // CRC check failed
        }
    } else {
        // Serial.println(F("No data available."));
        return false; // No data available
    }
}

bool Co2Sensor::readMeasurement() {
    if (!getDataReadyStatus()) {
        // Serial.println(F("Data not ready, skipping measurement read."));
        return false;
    }

    // Serial.println(F("Reading measurement data..."));
    uint8_t data[9];
    if (!writeCommand(SCD4x_COMMAND_READ_MEASUREMENT) || !readData(data, sizeof(data))) {
        unsigned long startTime = millis(); // Record the start time
        while (millis() - startTime < 1) {
            // Do nothing, just wait
        }
        // Serial.println(F("Failed to read measurement data."));
        return false;
    }

    processReadings(data);
    // Serial.println(F("Measurement read successfully."));
    return true;
}

void Co2Sensor::resetValues() {
    co2 = -1;
    co2Humidity = -1;
    temperature = -1;
   // Serial.println(F("Sensor values have been reset."));
}

void Co2Sensor::processReadings(const uint8_t* data) {
    co2 = (data[0] << 8) | data[1];
    uint16_t rawTemp = (data[3] << 8) | data[4];
    uint16_t rawHumidity = (data[6] << 8) | data[7];
    
    // Convert raw temperature to Celsius
    float temperatureCelsius = -45 + (175.0 * rawTemp / 65535.0);
    // Then convert Celsius to Fahrenheit
    float temperatureFahrenheit = (temperatureCelsius * 9.0 / 5.0) + 32;
    // Store temperature as a 3-digit integer
    temperature = static_cast<int>(temperatureFahrenheit * 10);
    
    // Convert raw humidity to percentage
    float humidityPercent = 100.0 * rawHumidity / 65535.0;
    // Store humidity as a 3-digit integer
    co2Humidity = static_cast<int>(humidityPercent * 10);
}

bool Co2Sensor::readData(uint8_t* data, uint8_t len) {
    Wire.requestFrom(static_cast<uint8_t>(I2CCo2), len);
    int i = 0;
    while (Wire.available() && i < len) {
        data[i++] = Wire.read();
    }

    if (i != len) return false; // Not enough data

    // Perform CRC check
    for (int j = 0; j < len; j += 3) {
        if (computeCRC8(data + j, 2) != data[j + 2]) return false; // CRC mismatch
    }

    return true;
}
uint8_t Co2Sensor::computeCRC8(uint8_t data[], uint8_t len) {
    uint8_t crc = 0xFF; // Initialize with 0xFF

    for (uint8_t x = 0; x < len; x++) {
        crc ^= data[x]; // XOR-in the next input byte

        for (uint8_t i = 0; i < 8; i++) {
            if ((crc & 0x80) != 0) crc = (uint8_t)((crc << 1) ^ 0x31);
            else crc <<= 1;
        }
    }

    return crc; // No output reflection
}
int Co2Sensor::getCO2Level() {
  
    return co2;
    
}

int Co2Sensor::getHumidity() {
    // Returns the humidity as a percentage in a 3-digit integer format
    return co2Humidity; // No further conversion needed
}
int Co2Sensor::getTempLevel() {
    // Returns the temperature in Fahrenheit as a 3-digit integer
    return temperature; // No further conversion needed
}
bool Co2Sensor::performFactoryReset() {
    Serial.println(F("Attempting factory reset."));
    if (!writeCommand(SCD4x_COMMAND_PERFORM_FACTORY_RESET)) {
        Serial.println(F("Factory reset command failed."));
        return false;
    }

    unsigned long startTime = millis(); // Record the start time
    while (millis() - startTime < 1500) {
        // Do nothing, just wait
    }

    Serial.println(F("Factory reset likely successful."));
    return true;
}
