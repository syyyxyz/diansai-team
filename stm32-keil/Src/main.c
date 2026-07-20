#include <stdint.h>

#include "calibration_store.h"
#include "fpga_dds.h"
#include "model_learning.h"
#include "oled_ssd1306.h"
#include "stm32f407_min.h"

#define SWEEP_AMPLITUDE_MVPP       1000UL
#define SWEEP_MIN_AMPLITUDE_MVPP    125UL
#define SWEEP_MEASURE_CYCLES         100U
#define SWEEP_SETTLE_TIME_MS         500U
#define STATUS_POLL_TIME_MS           10UL
#define MEASUREMENT_RETRIES             4U
#define CALIBRATION_PREPARE_MS        5000UL
#define ANALYSIS_PAGE_TIME_MS         3000UL

#define SYST_CSR                   (*(volatile uint32_t *)0xE000E010UL)
#define SYST_RVR                   (*(volatile uint32_t *)0xE000E014UL)
#define SYST_CVR                   (*(volatile uint32_t *)0xE000E018UL)
#define SYST_CSR_ENABLE            (1UL << 0)
#define SYST_CSR_CLKSOURCE         (1UL << 2)
#define SYST_CSR_COUNTFLAG         (1UL << 16)

extern uint32_t SystemCoreClock;

volatile fpga_dds_status_t g_fpga_last_error = FPGA_DDS_OK;
volatile fpga_measure_status_t g_fpga_last_status;
volatile fpga_measure_result_t g_fpga_last_result;
volatile uint32_t g_fpga_rx_ok_count = 0UL;
volatile uint32_t g_fpga_rx_error_count = 0UL;

static calibration_table_t calibration_table;
static model_point_t model_points[MODEL_POINT_COUNT];
static model_analysis_t model_analysis;
static uint8_t measurement_sequence = 0U;

static void delay_init(void)
{
    SYST_CSR = 0UL;
    SYST_RVR = SystemCoreClock / 1000UL - 1UL;
    SYST_CVR = 0UL;
    SYST_CSR = SYST_CSR_CLKSOURCE | SYST_CSR_ENABLE;
}

static void delay_ms(uint32_t milliseconds)
{
    while (milliseconds > 0UL) {
        while ((SYST_CSR & SYST_CSR_COUNTFLAG) == 0UL) {
        }
        --milliseconds;
    }
}

static void recalibration_button_init(void)
{
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    GPIOA_MODER &= ~(3UL << 0U);
    GPIOA_PUPDR &= ~(3UL << 0U);
    GPIOA_PUPDR |= (2UL << 0U);
}

static uint8_t recalibration_requested(void)
{
    uint32_t elapsed = 0UL;

    if ((GPIOA_IDR & 1UL) == 0UL) {
        return 0U;
    }
    while (elapsed < 1200UL) {
        delay_ms(10UL);
        elapsed += 10UL;
        if ((GPIOA_IDR & 1UL) == 0UL) {
            return 0U;
        }
    }
    return 1U;
}

static const char *error_text(fpga_dds_status_t error)
{
    switch (error) {
        case FPGA_DDS_ERROR_FREQUENCY:      return "BAD FREQUENCY";
        case FPGA_DDS_ERROR_AMPLITUDE:      return "BAD AMPLITUDE";
        case FPGA_DDS_ERROR_ARGUMENT:       return "BAD ARGUMENT";
        case FPGA_DDS_ERROR_SPI_TIMEOUT:    return "SPI TIMEOUT";
        case FPGA_DDS_ERROR_FRAME:          return "FRAME HEADER";
        case FPGA_DDS_ERROR_CRC:            return "CRC ERROR";
        case FPGA_DDS_ERROR_SEQUENCE:       return "SEQ ERROR";
        case FPGA_DDS_ERROR_FPGA_PROTOCOL:  return "FPGA REJECT";
        case FPGA_DDS_ERROR_MEASURE_TIMEOUT:return "MEASURE TIMEOUT";
        default:                            return "UNKNOWN ERROR";
    }
}

static void display_error(uint8_t oled_ready,
                          fpga_dds_status_t error,
                          uint32_t frequency_hz)
{
    if (oled_ready == 0U) {
        return;
    }
    OLED_Clear();
    OLED_Print(0U, 0U, "MEASUREMENT ERROR");
    OLED_Print(0U, 2U, error_text(error));
    OLED_Print(0U, 4U, "F:");
    OLED_PrintU32(2U, 4U, frequency_hz);
    OLED_Print(7U, 4U, "HZ");
    OLED_Print(0U, 6U, "CHECK FPGA AND SPI");
    OLED_Print(0U, 7U, "RESET TO RETRY");
    (void)OLED_Update();
}

static void display_sweep_progress(uint8_t oled_ready,
                                   uint8_t calibration,
                                   uint32_t index,
                                   uint32_t frequency_hz,
                                   uint32_t amplitude_mvpp)
{
    if (oled_ready == 0U) {
        return;
    }
    OLED_Clear();
    OLED_Print(0U, 0U, calibration != 0U ? "BYPASS CALIBRATION" :
                                          "UNKNOWN SWEEP");
    OLED_Print(0U, 2U, "POINT:");
    OLED_PrintU32(6U, 2U, index + 1UL);
    OLED_Print(9U, 2U, "/30");
    OLED_Print(0U, 3U, "F:");
    OLED_PrintU32(2U, 3U, frequency_hz);
    OLED_Print(7U, 3U, "HZ");
    OLED_Print(0U, 4U, "DRIVE:");
    OLED_PrintU32(6U, 4U, amplitude_mvpp);
    OLED_Print(11U, 4U, "MVPP");
    OLED_Print(0U, 6U, "MEASURING I AND Q");
    OLED_Print(0U, 7U, "DO NOT CHANGE WIRING");
    (void)OLED_Update();
}

static void print_gain(uint8_t column, uint8_t row, float gain)
{
    uint32_t scaled;
    uint32_t whole;
    uint32_t fractional;
    uint8_t decimal_column;

    if (gain < 0.0f) {
        gain = 0.0f;
    }
    if (gain > 99.99f) {
        gain = 99.99f;
    }
    scaled = (uint32_t)(gain * 100.0f + 0.5f);
    whole = scaled / 100UL;
    fractional = scaled % 100UL;
    OLED_PrintU32(column, row, whole);
    decimal_column = (uint8_t)(column + ((whole >= 10UL) ? 2U : 1U));
    OLED_Print(decimal_column, row, ".");
    if (fractional < 10UL) {
        OLED_Print((uint8_t)(decimal_column + 1U), row, "0");
        OLED_PrintU32((uint8_t)(decimal_column + 2U), row, fractional);
    } else {
        OLED_PrintU32((uint8_t)(decimal_column + 1U), row, fractional);
    }
}

static void print_phase(uint8_t column, uint8_t row, float phase_deg)
{
    int64_t rounded = (int64_t)((phase_deg >= 0.0f) ?
                      phase_deg + 0.5f : phase_deg - 0.5f);
    OLED_PrintS64(column, row, rounded);
}

static void display_response_line(uint8_t row,
                                  const char *label,
                                  uint32_t index)
{
    OLED_Print(0U, row, label);
    if ((index >= MODEL_POINT_COUNT) || (model_points[index].valid == 0U)) {
        OLED_Print(8U, row, "INVALID");
        return;
    }
    print_gain(8U, row, model_points[index].gain);
    OLED_Print(13U, row, "P:");
    print_phase(15U, row, model_points[index].phase_deg);
}

static void display_analysis_summary(uint8_t oled_ready)
{
    if (oled_ready == 0U) {
        return;
    }
    OLED_Clear();
    OLED_Print(0U, 0U, "UNKNOWN MODEL READY");
    OLED_Print(0U, 1U, "TYPE:");
    OLED_Print(5U, 1U, Model_ResponseText(model_analysis.type));
    OLED_Print(0U, 2U, "VALID:");
    OLED_PrintU32(6U, 2U, model_analysis.valid_points);
    OLED_Print(9U, 2U, "/30");
    if (model_analysis.feature_frequency_hz != 0UL) {
        OLED_Print(0U, 3U, "FEATURE:");
        OLED_PrintU32(8U, 3U, model_analysis.feature_frequency_hz);
        OLED_Print(13U, 3U, "HZ");
    } else {
        OLED_Print(0U, 3U, "FEATURE:N/A");
    }
    display_response_line(4U, "100HZ G:", 0U);
    display_response_line(5U, "1KHZ  G:", 9U);
    display_response_line(6U, "3KHZ  G:", 29U);
    OLED_Print(0U, 7U, "PA0+RESET RECAL");
    (void)OLED_Update();
}

static void display_analysis_extrema(uint8_t oled_ready)
{
    if (oled_ready == 0U) {
        return;
    }
    OLED_Clear();
    OLED_Print(0U, 0U, "MODEL EXTREMA");
    OLED_Print(0U, 2U, "MAX G:");
    print_gain(6U, 2U, model_analysis.peak_gain);
    OLED_Print(0U, 3U, "MAX F:");
    OLED_PrintU32(6U, 3U, model_analysis.peak_frequency_hz);
    OLED_Print(11U, 3U, "HZ");
    OLED_Print(0U, 4U, "MIN G:");
    print_gain(6U, 4U, model_analysis.minimum_gain);
    OLED_Print(0U, 5U, "MIN F:");
    OLED_PrintU32(6U, 5U, model_analysis.minimum_frequency_hz);
    OLED_Print(11U, 5U, "HZ");
    OLED_Print(0U, 7U, "PA0+RESET RECAL");
    (void)OLED_Update();
}

static fpga_dds_status_t run_measurement(uint8_t sequence,
                                         uint32_t timeout_ms,
                                         fpga_measure_result_t *result)
{
    fpga_measure_status_t status;
    fpga_dds_status_t error;
    uint32_t elapsed = 0UL;

    error = FPGA_DDS_ClearResult();
    if (error != FPGA_DDS_OK) {
        return error;
    }
    error = FPGA_DDS_StartMeasurement(sequence,
                                      SWEEP_MEASURE_CYCLES,
                                      SWEEP_SETTLE_TIME_MS);
    if (error != FPGA_DDS_OK) {
        return error;
    }

    while (elapsed < timeout_ms) {
        delay_ms(STATUS_POLL_TIME_MS);
        elapsed += STATUS_POLL_TIME_MS;
        error = FPGA_DDS_ReadStatus(&status);
        if (error != FPGA_DDS_OK) {
            return error;
        }
        g_fpga_last_status = status;

        if ((status.sequence == sequence) &&
            ((status.flags & FPGA_MEASURE_STATUS_PROTOCOL_ERROR) != 0U)) {
            return FPGA_DDS_ERROR_FPGA_PROTOCOL;
        }
        if ((status.sequence == sequence) &&
            ((status.flags & (FPGA_MEASURE_STATUS_BUSY |
                              FPGA_MEASURE_STATUS_DONE |
                              FPGA_MEASURE_STATUS_VALID)) ==
                             (FPGA_MEASURE_STATUS_DONE |
                              FPGA_MEASURE_STATUS_VALID))) {
            return FPGA_DDS_ReadResult(sequence, result);
        }
    }
    return FPGA_DDS_ERROR_MEASURE_TIMEOUT;
}

static fpga_dds_status_t measure_frequency(uint32_t frequency_hz,
                                           uint32_t amplitude_mvpp,
                                           fpga_measure_result_t *result)
{
    fpga_dds_status_t error;
    uint32_t measurement_ms;
    uint32_t timeout_ms;

    error = FPGA_DDS_SetSine(frequency_hz, amplitude_mvpp);
    if (error != FPGA_DDS_OK) {
        return error;
    }

    measurement_ms = ((uint32_t)SWEEP_MEASURE_CYCLES * 1000UL +
                      frequency_hz - 1UL) / frequency_hz;
    timeout_ms = (uint32_t)SWEEP_SETTLE_TIME_MS + measurement_ms + 1000UL;
    ++measurement_sequence;
    error = run_measurement(measurement_sequence, timeout_ms, result);
    if ((error == FPGA_DDS_OK) &&
        (result->frequency_word != FPGA_DDS_FrequencyToFtw(frequency_hz))) {
        return FPGA_DDS_ERROR_SEQUENCE;
    }
    return error;
}

static fpga_dds_status_t acquire_calibration(uint8_t oled_ready)
{
    fpga_measure_result_t result;
    fpga_dds_status_t error = FPGA_DDS_ERROR_MEASURE_TIMEOUT;
    uint32_t frequency_hz;
    uint32_t index;
    uint32_t retry;

    for (index = 0U; index < MODEL_POINT_COUNT; ++index) {
        frequency_hz = Model_FrequencyAt(index);
        display_sweep_progress(oled_ready, 1U, index, frequency_hz,
                               SWEEP_AMPLITUDE_MVPP);
        for (retry = 0U; retry < MEASUREMENT_RETRIES; ++retry) {
            error = measure_frequency(frequency_hz,
                                      SWEEP_AMPLITUDE_MVPP,
                                      &result);
            g_fpga_last_error = error;
            if ((error == FPGA_DDS_OK) &&
                (Model_MeasurementUsable(&result, 1U) != 0U)) {
                g_fpga_last_result = result;
                ++g_fpga_rx_ok_count;
                Model_CalibrationSetPoint(&calibration_table,
                                          index,
                                          frequency_hz,
                                          SWEEP_AMPLITUDE_MVPP,
                                          &result);
                break;
            }
            ++g_fpga_rx_error_count;
        }
        if (retry == MEASUREMENT_RETRIES) {
            return (error == FPGA_DDS_OK) ? FPGA_DDS_ERROR_ARGUMENT : error;
        }
    }
    return FPGA_DDS_OK;
}

static void acquire_unknown_model(uint8_t oled_ready)
{
    fpga_measure_result_t result;
    fpga_dds_status_t error;
    uint32_t frequency_hz;
    uint32_t amplitude_mvpp;
    uint32_t index;
    uint32_t retry;

    for (index = 0U; index < MODEL_POINT_COUNT; ++index) {
        frequency_hz = Model_FrequencyAt(index);
        amplitude_mvpp = SWEEP_AMPLITUDE_MVPP;
        model_points[index].frequency_hz = frequency_hz;
        model_points[index].gain = 0.0f;
        model_points[index].phase_deg = 0.0f;
        model_points[index].valid = 0U;

        for (retry = 0U; retry < MEASUREMENT_RETRIES; ++retry) {
            display_sweep_progress(oled_ready, 0U, index, frequency_hz,
                                   amplitude_mvpp);
            error = measure_frequency(frequency_hz, amplitude_mvpp, &result);
            g_fpga_last_error = error;
            if ((error == FPGA_DDS_OK) &&
                ((result.flags & FPGA_MEASURE_STATUS_CLIP) != 0U) &&
                (amplitude_mvpp > SWEEP_MIN_AMPLITUDE_MVPP)) {
                amplitude_mvpp /= 2UL;
                continue;
            }
            if ((error == FPGA_DDS_OK) &&
                (Model_ComputePoint(&calibration_table.points[index],
                                    &result,
                                    amplitude_mvpp,
                                    &model_points[index]) != 0U)) {
                g_fpga_last_result = result;
                ++g_fpga_rx_ok_count;
                break;
            }
            ++g_fpga_rx_error_count;
        }
    }

    (void)FPGA_DDS_SetSine(MODEL_START_FREQUENCY_HZ, 0UL);
    Model_Analyze(model_points, &model_analysis);
}

static void halt_after_error(uint8_t oled_ready,
                             fpga_dds_status_t error,
                             uint32_t frequency_hz)
{
    (void)FPGA_DDS_SetSine(MODEL_START_FREQUENCY_HZ, 0UL);
    display_error(oled_ready, error, frequency_hz);
    for (;;) {
    }
}

int main(void)
{
    fpga_dds_status_t error;
    uint8_t oled_ready;
    uint8_t calibration_valid;

    delay_init();
    FPGA_DDS_Init();
    recalibration_button_init();
    delay_ms(300UL);
    oled_ready = OLED_Init();

    calibration_valid = CalibrationStore_Load(&calibration_table);
    if (recalibration_requested() != 0U) {
        if (CalibrationStore_Erase() == 0U) {
            halt_after_error(oled_ready, FPGA_DDS_ERROR_ARGUMENT, 0UL);
        }
        calibration_valid = 0U;
    }

    if (calibration_valid == 0U) {
        if (oled_ready != 0U) {
            OLED_Clear();
            OLED_Print(0U, 0U, "BYPASS CAL REQUIRED");
            OLED_Print(0U, 2U, "CONNECT DA TO AD");
            OLED_Print(0U, 4U, "CAL STARTS IN 5 SEC");
            OLED_Print(0U, 6U, "KEEP WIRE CONNECTED");
            (void)OLED_Update();
        }
        delay_ms(CALIBRATION_PREPARE_MS);
        error = acquire_calibration(oled_ready);
        if (error != FPGA_DDS_OK) {
            halt_after_error(oled_ready, error, 0UL);
        }
        (void)FPGA_DDS_SetSine(MODEL_START_FREQUENCY_HZ, 0UL);
        if ((CalibrationStore_Save(&calibration_table) == 0U) ||
            (CalibrationStore_Load(&calibration_table) == 0U)) {
            halt_after_error(oled_ready, FPGA_DDS_ERROR_ARGUMENT, 0UL);
        }

        if (oled_ready != 0U) {
            OLED_Clear();
            OLED_Print(0U, 0U, "CALIBRATION SAVED");
            OLED_Print(0U, 2U, "POWER OFF BOARD");
            OLED_Print(0U, 3U, "CONNECT UNKNOWN");
            OLED_Print(0U, 5U, "THEN RESET BOARD");
            OLED_Print(0U, 7U, "DO NOT REFLASH");
            (void)OLED_Update();
        }
        for (;;) {
        }
    }

    if (oled_ready != 0U) {
        OLED_Clear();
        OLED_Print(0U, 0U, "CAL TABLE LOADED");
        OLED_Print(0U, 2U, "UNKNOWN CIRCUIT");
        OLED_Print(0U, 3U, "ANALYSIS STARTING");
        OLED_Print(0U, 6U, "30 POINT COMPLEX H");
        (void)OLED_Update();
    }
    delay_ms(1000UL);
    acquire_unknown_model(oled_ready);

    for (;;) {
        display_analysis_summary(oled_ready);
        delay_ms(ANALYSIS_PAGE_TIME_MS);
        display_analysis_extrema(oled_ready);
        delay_ms(ANALYSIS_PAGE_TIME_MS);
    }
}
