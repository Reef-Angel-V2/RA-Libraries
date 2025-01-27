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

#ifndef CO2_H
#define CO2_H

//#include "Globals.h"
#include <Wire.h>
#include <Arduino.h>
//Basic Commands
#define SCD4x_COMMAND_START_PERIODIC_MEASUREMENT              0x21b1
#define SCD4x_COMMAND_READ_MEASUREMENT                        0xec05 
#define SCD4x_COMMAND_STOP_PERIODIC_MEASUREMENT               0x3f86 
#define SCD4x_COMMAND_SET_AUTOMATIC_SELF_CALIBRATION_ENABLED  0x2416 
#define SCD4x_COMMAND_PERFORM_SELF_TEST                       0x3639 
#define SCD4x_COMMAND_PERFORM_FACTORY_RESET                   0x3632 
#define SCD4x_COMMAND_GET_DATA_READY_STATUS                   0xE4B8



#define I2CCo2          0x62


class Co2Sensor {
public:
    Co2Sensor();
    bool begin(bool startMeasurements);
    bool readMeasurement();
    bool performFactoryReset();
    int getCO2Level();
    int getHumidity();
    int getTempLevel();
    int co2ppmLastLevel;
    int co2HumidityLastLevel;
    int co2 = 0;
    int temperature = 0;
    int co2Humidity = 0;
    
    
private:
    bool readRegister(uint16_t registerAddress, uint16_t* response, uint16_t delayMillis = 1);
    bool initialized = false;
    bool writeCommand(uint16_t command);
    bool readData(uint8_t* data, uint8_t len);
    void resetValues();
    void processReadings(const uint8_t* data);
    bool getDataReadyStatus();
    uint8_t computeCRC8(uint8_t* data, uint8_t len);
};


#endif


