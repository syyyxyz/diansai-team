#ifndef FPGA_DDS_H
#define FPGA_DDS_H

#include <stdint.h>

#define FPGA_DDS_SAMPLE_CLOCK_HZ     125000000UL
#define FPGA_DDS_MIN_FREQUENCY_HZ    1UL
#define FPGA_DDS_MAX_FREQUENCY_HZ    60000000UL

/*
 * Calibrate this value with an oscilloscope at low frequency and full-scale
 * gain. It includes the AD9708 module and any output amplifier after it.
 * The default matches a 3.000 Vpp full-scale analog chain.
 */
#ifndef FPGA_DDS_FULL_SCALE_MVPP
#define FPGA_DDS_FULL_SCALE_MVPP     3000UL
#endif

typedef enum {
    FPGA_DDS_OK = 0,
    FPGA_DDS_ERROR_FREQUENCY,
    FPGA_DDS_ERROR_AMPLITUDE,
    FPGA_DDS_ERROR_ARGUMENT,
    FPGA_DDS_ERROR_SPI_TIMEOUT,
    FPGA_DDS_ERROR_FRAME,
    FPGA_DDS_ERROR_CRC,
    FPGA_DDS_ERROR_SEQUENCE,
    FPGA_DDS_ERROR_FPGA_PROTOCOL,
    FPGA_DDS_ERROR_MEASURE_TIMEOUT
} fpga_dds_status_t;

#define FPGA_MEASURE_STATUS_BUSY           (1U << 0)
#define FPGA_MEASURE_STATUS_DONE           (1U << 1)
#define FPGA_MEASURE_STATUS_VALID          (1U << 2)
#define FPGA_MEASURE_STATUS_CLIP           (1U << 3)
#define FPGA_MEASURE_STATUS_OVERFLOW       (1U << 4)
#define FPGA_MEASURE_STATUS_PROTOCOL_ERROR (1U << 5)

typedef struct {
    uint8_t sequence;
    uint8_t flags;
} fpga_measure_status_t;

typedef struct {
    uint8_t sequence;
    uint64_t frequency_word;
    int64_t i_accumulator;
    int64_t q_accumulator;
    uint32_t sample_count;
    uint8_t adc_min;
    uint8_t adc_max;
    uint8_t flags;
} fpga_measure_result_t;

void FPGA_DDS_Init(void);
fpga_dds_status_t FPGA_DDS_SetSine(uint32_t frequency_hz,
                                  uint32_t amplitude_mvpp);
fpga_dds_status_t FPGA_DDS_StartMeasurement(uint8_t sequence,
                                            uint16_t cycles,
                                            uint16_t settle_ms);
fpga_dds_status_t FPGA_DDS_ReadStatus(fpga_measure_status_t *status);
fpga_dds_status_t FPGA_DDS_ReadResult(uint8_t expected_sequence,
                                     fpga_measure_result_t *result);
fpga_dds_status_t FPGA_DDS_ClearResult(void);
uint64_t FPGA_DDS_FrequencyToFtw(uint32_t frequency_hz);
uint16_t FPGA_DDS_AmplitudeToQ16(uint32_t amplitude_mvpp);

#endif
