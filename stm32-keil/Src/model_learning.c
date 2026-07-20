#include "model_learning.h"

#define MODEL_BAD_STATUS_MASK (FPGA_MEASURE_STATUS_BUSY | \
                               FPGA_MEASURE_STATUS_CLIP | \
                               FPGA_MEASURE_STATUS_OVERFLOW | \
                               FPGA_MEASURE_STATUS_PROTOCOL_ERROR)
#define MODEL_REQUIRED_STATUS (FPGA_MEASURE_STATUS_DONE | \
                               FPGA_MEASURE_STATUS_VALID)

static uint64_t magnitude_i64(int64_t value)
{
    if (value < 0) {
        return (uint64_t)(-(value + 1)) + 1ULL;
    }
    return (uint64_t)value;
}

static float absolute_float(float value)
{
    return (value < 0.0f) ? -value : value;
}

static float square_root(float value)
{
    float estimate;
    uint32_t iteration;

    if (value <= 0.0f) {
        return 0.0f;
    }

    estimate = (value >= 1.0f) ? value : 1.0f;
    for (iteration = 0U; iteration < 12U; ++iteration) {
        estimate = 0.5f * (estimate + value / estimate);
    }
    return estimate;
}

static float arctangent_degrees(float value)
{
    float magnitude = absolute_float(value);
    float angle;

    if (magnitude <= 1.0f) {
        angle = magnitude * (45.0f + 15.6423f * (1.0f - magnitude));
    } else {
        magnitude = 1.0f / magnitude;
        angle = 90.0f -
                magnitude * (45.0f + 15.6423f * (1.0f - magnitude));
    }
    return (value < 0.0f) ? -angle : angle;
}

static float phase_degrees(float imaginary, float real)
{
    float angle;

    if (real > 0.0f) {
        return arctangent_degrees(imaginary / real);
    }
    if (real < 0.0f) {
        angle = arctangent_degrees(imaginary / real);
        return (imaginary >= 0.0f) ? angle + 180.0f : angle - 180.0f;
    }
    if (imaginary > 0.0f) {
        return 90.0f;
    }
    if (imaginary < 0.0f) {
        return -90.0f;
    }
    return 0.0f;
}

static float band_average(const model_point_t points[MODEL_POINT_COUNT],
                          uint32_t first,
                          uint32_t last)
{
    float sum = 0.0f;
    uint32_t count = 0U;
    uint32_t index;

    for (index = first; index <= last; ++index) {
        if (points[index].valid != 0U) {
            sum += points[index].gain;
            ++count;
        }
    }
    return (count == 0U) ? 0.0f : sum / (float)count;
}

uint32_t Model_FrequencyAt(uint32_t index)
{
    return MODEL_START_FREQUENCY_HZ + index * MODEL_FREQUENCY_STEP_HZ;
}

uint8_t Model_MeasurementUsable(const fpga_measure_result_t *result,
                                uint8_t require_signal)
{
    uint64_t signal;

    if (result == 0) {
        return 0U;
    }
    if (((result->flags & MODEL_BAD_STATUS_MASK) != 0U) ||
        ((result->flags & MODEL_REQUIRED_STATUS) != MODEL_REQUIRED_STATUS) ||
        (result->sample_count == 0UL) ||
        (result->adc_max <= result->adc_min)) {
        return 0U;
    }

    if (require_signal == 0U) {
        return 1U;
    }

    signal = magnitude_i64(result->i_accumulator) +
             magnitude_i64(result->q_accumulator);
    if (((uint32_t)result->adc_max - (uint32_t)result->adc_min < 8UL) ||
        (signal < (uint64_t)result->sample_count * 32ULL)) {
        return 0U;
    }
    return 1U;
}

void Model_CalibrationSetPoint(calibration_table_t *table,
                               uint32_t index,
                               uint32_t frequency_hz,
                               uint32_t amplitude_mvpp,
                               const fpga_measure_result_t *result)
{
    calibration_point_t *point;

    if ((table == 0) || (result == 0) || (index >= MODEL_POINT_COUNT)) {
        return;
    }

    point = &table->points[index];
    point->i_accumulator = result->i_accumulator;
    point->q_accumulator = result->q_accumulator;
    point->sample_count = result->sample_count;
    point->frequency_hz = frequency_hz;
    point->amplitude_mvpp = amplitude_mvpp;
    point->reserved = 0UL;
}

uint8_t Model_ComputePoint(const calibration_point_t *calibration,
                           const fpga_measure_result_t *measurement,
                           uint32_t amplitude_mvpp,
                           model_point_t *point)
{
    float bypass_i;
    float bypass_q;
    float unknown_i;
    float unknown_q;
    float denominator;
    float scale;
    float response_real;
    float response_imaginary;

    if ((calibration == 0) || (measurement == 0) || (point == 0) ||
        (calibration->sample_count == 0UL) ||
        (calibration->amplitude_mvpp == 0UL) ||
        (amplitude_mvpp == 0UL) ||
        (Model_MeasurementUsable(measurement, 0U) == 0U)) {
        return 0U;
    }

    bypass_i = (float)calibration->i_accumulator;
    bypass_q = (float)calibration->q_accumulator;
    unknown_i = (float)measurement->i_accumulator;
    unknown_q = (float)measurement->q_accumulator;
    denominator = bypass_i * bypass_i + bypass_q * bypass_q;
    if (denominator <= 1.0f) {
        return 0U;
    }

    scale = ((float)calibration->sample_count *
             (float)calibration->amplitude_mvpp) /
            ((float)measurement->sample_count * (float)amplitude_mvpp);
    response_real = (unknown_i * bypass_i + unknown_q * bypass_q) /
                    denominator * scale;
    response_imaginary = (unknown_q * bypass_i - unknown_i * bypass_q) /
                         denominator * scale;

    point->frequency_hz = calibration->frequency_hz;
    point->gain = square_root(response_real * response_real +
                              response_imaginary * response_imaginary);
    point->phase_deg = phase_degrees(response_imaginary, response_real);
    point->valid = 1U;
    point->reserved[0] = 0U;
    point->reserved[1] = 0U;
    point->reserved[2] = 0U;
    return 1U;
}

void Model_Analyze(const model_point_t points[MODEL_POINT_COUNT],
                   model_analysis_t *analysis)
{
    float low;
    float middle;
    float high;
    float pass_gain;
    float threshold;
    float dynamic_ratio;
    uint32_t peak_index = 0U;
    uint32_t minimum_index = 0U;
    uint32_t index;
    uint8_t found = 0U;

    if (analysis == 0) {
        return;
    }

    analysis->type = MODEL_RESPONSE_UNKNOWN;
    analysis->feature_frequency_hz = 0UL;
    analysis->peak_frequency_hz = 0UL;
    analysis->minimum_frequency_hz = 0UL;
    analysis->peak_gain = 0.0f;
    analysis->minimum_gain = 0.0f;
    analysis->valid_points = 0U;

    for (index = 0U; index < MODEL_POINT_COUNT; ++index) {
        if (points[index].valid == 0U) {
            continue;
        }
        if (found == 0U) {
            peak_index = index;
            minimum_index = index;
            found = 1U;
        } else {
            if (points[index].gain > points[peak_index].gain) {
                peak_index = index;
            }
            if (points[index].gain < points[minimum_index].gain) {
                minimum_index = index;
            }
        }
        ++analysis->valid_points;
    }

    if (found == 0U) {
        return;
    }

    analysis->peak_gain = points[peak_index].gain;
    analysis->minimum_gain = points[minimum_index].gain;
    analysis->peak_frequency_hz = points[peak_index].frequency_hz;
    analysis->minimum_frequency_hz = points[minimum_index].frequency_hz;
    if (analysis->valid_points < 20U) {
        return;
    }

    low = band_average(points, 0U, 4U);
    middle = band_average(points, 12U, 17U);
    high = band_average(points, 25U, 29U);
    dynamic_ratio = (analysis->minimum_gain > 0.0001f) ?
                    analysis->peak_gain / analysis->minimum_gain : 10000.0f;

    if ((dynamic_ratio <= 1.35f) &&
        (absolute_float(low - high) <= 0.25f * (low + high))) {
        analysis->type = MODEL_RESPONSE_FLAT;
    } else if ((middle > low * 1.40f) && (middle > high * 1.40f)) {
        analysis->type = MODEL_RESPONSE_BAND_PASS;
    } else if ((low > middle * 1.40f) && (high > middle * 1.40f)) {
        analysis->type = MODEL_RESPONSE_BAND_STOP;
    } else if (low > high * 1.80f) {
        analysis->type = MODEL_RESPONSE_LOW_PASS;
    } else if (high > low * 1.80f) {
        analysis->type = MODEL_RESPONSE_HIGH_PASS;
    }

    if (analysis->type == MODEL_RESPONSE_LOW_PASS) {
        pass_gain = low;
        threshold = pass_gain * 0.7071068f;
        for (index = 0U; index < MODEL_POINT_COUNT; ++index) {
            if ((points[index].valid != 0U) &&
                (points[index].gain <= threshold)) {
                analysis->feature_frequency_hz = points[index].frequency_hz;
                break;
            }
        }
    } else if (analysis->type == MODEL_RESPONSE_HIGH_PASS) {
        pass_gain = high;
        threshold = pass_gain * 0.7071068f;
        for (index = 0U; index < MODEL_POINT_COUNT; ++index) {
            if ((points[index].valid != 0U) &&
                (points[index].gain >= threshold)) {
                analysis->feature_frequency_hz = points[index].frequency_hz;
                break;
            }
        }
    } else if (analysis->type == MODEL_RESPONSE_BAND_PASS) {
        analysis->feature_frequency_hz = analysis->peak_frequency_hz;
    } else if (analysis->type == MODEL_RESPONSE_BAND_STOP) {
        analysis->feature_frequency_hz = analysis->minimum_frequency_hz;
    }
}

const char *Model_ResponseText(model_response_type_t type)
{
    switch (type) {
        case MODEL_RESPONSE_FLAT:      return "FLAT";
        case MODEL_RESPONSE_LOW_PASS:  return "LOW PASS";
        case MODEL_RESPONSE_HIGH_PASS: return "HIGH PASS";
        case MODEL_RESPONSE_BAND_PASS: return "BAND PASS";
        case MODEL_RESPONSE_BAND_STOP: return "BAND STOP";
        default:                       return "UNKNOWN";
    }
}
