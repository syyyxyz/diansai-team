#ifndef MODEL_LEARNING_H
#define MODEL_LEARNING_H

#include <stdint.h>

#include "fpga_dds.h"

#define MODEL_POINT_COUNT          30U
#define MODEL_START_FREQUENCY_HZ  100UL
#define MODEL_FREQUENCY_STEP_HZ   100UL
#define MODEL_END_FREQUENCY_HZ   3000UL

typedef struct {
    int64_t i_accumulator;
    int64_t q_accumulator;
    uint32_t sample_count;
    uint32_t frequency_hz;
    uint32_t amplitude_mvpp;
    uint32_t reserved;
} calibration_point_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t point_count;
    uint32_t reserved;
    calibration_point_t points[MODEL_POINT_COUNT];
    uint32_t crc32;
} calibration_table_t;

typedef struct {
    uint32_t frequency_hz;
    float gain;
    float phase_deg;
    uint8_t valid;
    uint8_t reserved[3];
} model_point_t;

typedef enum {
    MODEL_RESPONSE_UNKNOWN = 0,
    MODEL_RESPONSE_FLAT,
    MODEL_RESPONSE_LOW_PASS,
    MODEL_RESPONSE_HIGH_PASS,
    MODEL_RESPONSE_BAND_PASS,
    MODEL_RESPONSE_BAND_STOP
} model_response_type_t;

typedef struct {
    model_response_type_t type;
    uint32_t feature_frequency_hz;
    uint32_t peak_frequency_hz;
    uint32_t minimum_frequency_hz;
    float peak_gain;
    float minimum_gain;
    uint8_t valid_points;
} model_analysis_t;

uint32_t Model_FrequencyAt(uint32_t index);
uint8_t Model_MeasurementUsable(const fpga_measure_result_t *result,
                                uint8_t require_signal);
void Model_CalibrationSetPoint(calibration_table_t *table,
                               uint32_t index,
                               uint32_t frequency_hz,
                               uint32_t amplitude_mvpp,
                               const fpga_measure_result_t *result);
uint8_t Model_ComputePoint(const calibration_point_t *calibration,
                           const fpga_measure_result_t *measurement,
                           uint32_t amplitude_mvpp,
                           model_point_t *point);
void Model_Analyze(const model_point_t points[MODEL_POINT_COUNT],
                   model_analysis_t *analysis);
const char *Model_ResponseText(model_response_type_t type);

#endif
