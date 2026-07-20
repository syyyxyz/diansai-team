#ifndef CALIBRATION_STORE_H
#define CALIBRATION_STORE_H

#include <stdint.h>

#include "model_learning.h"

#define CALIBRATION_MAGIC   0x43414C31UL
#define CALIBRATION_VERSION 0x00010000UL

uint8_t CalibrationStore_Load(calibration_table_t *table);
uint8_t CalibrationStore_Save(calibration_table_t *table);
uint8_t CalibrationStore_Erase(void);

#endif
