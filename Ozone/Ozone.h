/*
 * Copyright 2024 Reef Angel / Brenny Cutler
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

#ifndef OZONE_H  // Check if OZONE_H is not defined
#define OZONE_H  // Define OZONE_H to prevent further inclusion

#include "Globals.h" // Include Globals.h to access i2cOzone
#include <Wire.h>

class OzoneClass {
public:
    OzoneClass();
    bool begin();
    void setActiveMode();
    int readOzoneLevel();

private:
    void writeRegister(uint8_t reg, uint8_t value);
    int i2cReadOzoneData();
    int OzoneLevel;
};

#endif // OZONE_H

