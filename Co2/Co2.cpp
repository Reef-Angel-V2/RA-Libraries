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

Co2Sensor::Co2Sensor() {
  
}

bool Co2Sensor::begin(bool startMeasurements) {
    static uint8_t state = 0;
    static unsigned long lastMillis = 0;
    const unsigned long stepDelay = 500;
    bool success = true;
    switch (state) {
        case 0:
            lastMillis = millis();
            state = 1;
            break;
        case 1:
            if (millis() - lastMillis >= stepDelay) {
                success &= writeCommand(SCD4x_COMMAND_STOP_PERIODIC_MEASUREMENT);
                lastMillis = millis();
                state = 2;
            }
            break;
        case 2:
            if (millis() - lastMillis >= stepDelay) {
                success &= writeCommand(SCD4x_COMMAND_SET_AUTOMATIC_SELF_CALIBRATION_ENABLED);
                lastMillis = millis();
                state = startMeasurements ? 3 : 4;
            }
            break;
        case 3:
            if (millis() - lastMillis >= stepDelay) {
                success &= writeCommand(SCD4x_COMMAND_START_PERIODIC_MEASUREMENT);
                lastMillis = millis();
                state = 4;
            }
            break;
        case 4:
            initialized = success;
            state = 0;
            return success;
    }
    return false;
}

bool Co2Sensor::readMeasurement() {
    if (!initialized) { // Retry until initialization succeeds
        if (begin(true)) {
        };
     }

    if (!getDataReadyStatus()) return false;

    uint8_t data[9];
    if (!writeCommand(SCD4x_COMMAND_READ_MEASUREMENT) || !readData(data, sizeof(data))) {
        return false;
    }
    processReadings(data);
    return true;
}

bool Co2Sensor::writeCommand(uint16_t command) {
    Wire.beginTransmission(I2CCo2);
    Wire.write(highByte(command));
    Wire.write(lowByte(command));
    return Wire.endTransmission() == 0;
}

bool Co2Sensor::getDataReadyStatus() {
    uint16_t response;
    if (!readRegister(SCD4x_COMMAND_GET_DATA_READY_STATUS, &response)) return false;
    return (response & 0x07FF) != 0;
}

bool Co2Sensor::readRegister(uint16_t registerAddress, uint16_t* response, uint16_t delayMillis) {
        if (!writeCommand(registerAddress)) {
        resetValues();
        initialized = false;
        return false;
    }

    delay(delayMillis); // Allow the sensor to process the request

    Wire.requestFrom(static_cast<uint8_t>(I2CCo2), static_cast<uint8_t>(3));
    unsigned long start = millis();
    while (Wire.available() < 3) { // Wait for response
        if (millis() - start > 100) { // Timeout after 100ms
            initialized = false; // Mark as uninitialized
            return false;
        }
    }

    uint8_t data[2] = { Wire.read(), Wire.read() };
    uint8_t crc = Wire.read();

    *response = (static_cast<uint16_t>(data[0]) << 8) | data[1];
    return crc == computeCRC8(data, 2);
}

bool Co2Sensor::readData(uint8_t* data, uint8_t len) {
    Wire.requestFrom(static_cast<uint8_t>(I2CCo2), len);
    for (int i = 0; i < len; i++) {
        if (!Wire.available()) return false;
        data[i] = Wire.read();
    }

    for (int i = 0; i < len; i += 3) {
        if (computeCRC8(data + i, 2) != data[i + 2]) return false; // CRC mismatch
    }

    return true;
}

void Co2Sensor::processReadings(const uint8_t* data) {
    co2 = (data[0] << 8) | data[1];
    uint16_t rawTemp = (data[3] << 8) | data[4];
    uint16_t rawHumidity = (data[6] << 8) | data[7];

    // Convert raw temperature to Fahrenheit
    float tempCelsius = -45 + (175.0 * rawTemp / 65535.0);
    temperature = static_cast<int>((tempCelsius * 9.0 / 5.0) + 32) * 10;

    // Convert raw humidity to percentage
    co2Humidity = static_cast<int>((100.0 * rawHumidity / 65535.0) * 10);
}

uint8_t Co2Sensor::computeCRC8(uint8_t* data, uint8_t len) {
    uint8_t crc = 0xFF;

    for (uint8_t x = 0; x < len; x++) {
        crc ^= data[x];
        for (uint8_t i = 0; i < 8; i++) {
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : crc << 1;
        }
    }

    return crc;
}

int Co2Sensor::getCO2Level() {
    if (readMeasurement()) {
        return co2;
    }
  return co2;
}
int Co2Sensor::getHumidity() {

    if (readMeasurement()) {
        return co2Humidity;
    }
  return co2Humidity;
}
int Co2Sensor::getTempLevel() {
    if (readMeasurement()) {
        return temperature;
    }
  return temperature;
}
void Co2Sensor::resetValues() {
    co2 = 0;
    co2Humidity = 0;
    temperature = 0;
}
