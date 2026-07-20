#include <stdint.h>
#include "fpga_dds.h"
#include "oled_ssd1306.h"

#define TEST_FREQUENCY_HZ       1000UL
#define TEST_AMPLITUDE_MVPP     1000UL
#define TEST_MEASURE_CYCLES       32U
#define TEST_SETTLE_TIME_MS       100U
#define STATUS_POLL_TIME_MS        10UL
#define MEASURE_TIMEOUT_MS       1000UL
#define RESULT_DISPLAY_TIME_MS    500UL

#define SYST_CSR                   (*(volatile uint32_t *)0xE000E010UL)
#define SYST_RVR                   (*(volatile uint32_t *)0xE000E014UL)
#define SYST_CVR                   (*(volatile uint32_t *)0xE000E018UL)
#define SYST_CSR_ENABLE            (1UL << 0)
#define SYST_CSR_CLKSOURCE         (1UL << 2)
#define SYST_CSR_COUNTFLAG         (1UL << 16)

extern uint32_t SystemCoreClock;

/* These variables are also convenient to inspect in the Keil debugger. */
volatile fpga_dds_status_t g_fpga_last_error = FPGA_DDS_OK;
volatile fpga_measure_status_t g_fpga_last_status;
volatile fpga_measure_result_t g_fpga_last_result;
volatile uint32_t g_fpga_rx_ok_count = 0UL;
volatile uint32_t g_fpga_rx_error_count = 0UL;

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

static uint64_t signed_magnitude(int64_t value)
{
    if (value < 0) {
        return (uint64_t)(-(value + 1)) + 1ULL;
    }
    return (uint64_t)value;
}

static uint32_t quadrature_ratio_percent(const fpga_measure_result_t *result)
{
    uint64_t i_magnitude = signed_magnitude(result->i_accumulator);
    uint64_t q_magnitude = signed_magnitude(result->q_accumulator);
    uint64_t whole;
    uint64_t remainder;

    if (i_magnitude == 0ULL) {
        return 999UL;
    }

    whole = q_magnitude / i_magnitude;
    if (whole >= 10ULL) {
        return 999UL;
    }
    remainder = q_magnitude % i_magnitude;
    return (uint32_t)(whole * 100ULL +
                      (remainder * 100ULL) / i_magnitude);
}

static const char *error_text(fpga_dds_status_t error)
{
    switch (error) {
        case FPGA_DDS_ERROR_FREQUENCY:     return "BAD FREQUENCY";
        case FPGA_DDS_ERROR_AMPLITUDE:     return "BAD AMPLITUDE";
        case FPGA_DDS_ERROR_ARGUMENT:      return "BAD ARGUMENT";
        case FPGA_DDS_ERROR_SPI_TIMEOUT:   return "SPI TIMEOUT";
        case FPGA_DDS_ERROR_FRAME:         return "FRAME HEADER";
        case FPGA_DDS_ERROR_CRC:           return "CRC ERROR";
        case FPGA_DDS_ERROR_SEQUENCE:      return "SEQ ERROR";
        case FPGA_DDS_ERROR_FPGA_PROTOCOL: return "FPGA REJECT";
        case FPGA_DDS_ERROR_MEASURE_TIMEOUT:return "MEASURE TIMEOUT";
        default:                           return "UNKNOWN ERROR";
    }
}

static void display_startup(uint8_t oled_ready)
{
    if (oled_ready == 0U) {
        return;
    }
    OLED_Clear();
    OLED_Print(0U, 0U, "FPGA RX LOOP H=1");
    OLED_Print(0U, 2U, "STARTING 1KHZ TEST");
    OLED_Print(0U, 4U, "SPI1 PA4 PA5 PA6 PA7");
    OLED_Print(0U, 6U, "WAIT FOR FPGA DATA");
    (void)OLED_Update();
}

static void display_error(uint8_t oled_ready, fpga_dds_status_t error)
{
    if (oled_ready == 0U) {
        return;
    }
    OLED_Clear();
    OLED_Print(0U, 0U, "FPGA RX LOOP H=1");
    OLED_Print(0U, 1U, "RX:ERROR");
    OLED_Print(0U, 2U, error_text(error));
    OLED_Print(0U, 4U, "CHECK PA6 MISO");
    OLED_Print(0U, 5U, "CHECK CS SCK MOSI");
    OLED_Print(0U, 7U, "ERR:");
    OLED_PrintU32(4U, 7U, g_fpga_rx_error_count);
    (void)OLED_Update();
}

static void display_result(uint8_t oled_ready,
                           const fpga_measure_result_t *result)
{
    char status_text[] = "S:------";
    uint32_t ratio = quadrature_ratio_percent(result);
    uint8_t h1_pass;

    if (oled_ready == 0U) {
        return;
    }

    status_text[2] = ((result->flags & FPGA_MEASURE_STATUS_BUSY) != 0U) ? 'B' : '-';
    status_text[3] = ((result->flags & FPGA_MEASURE_STATUS_DONE) != 0U) ? 'D' : '-';
    status_text[4] = ((result->flags & FPGA_MEASURE_STATUS_VALID) != 0U) ? 'V' : '-';
    status_text[5] = ((result->flags & FPGA_MEASURE_STATUS_CLIP) != 0U) ? 'C' : '-';
    status_text[6] = ((result->flags & FPGA_MEASURE_STATUS_OVERFLOW) != 0U) ? 'O' : '-';
    status_text[7] = ((result->flags & FPGA_MEASURE_STATUS_PROTOCOL_ERROR) != 0U) ? 'P' : '-';

    h1_pass = (((result->flags & (FPGA_MEASURE_STATUS_BUSY |
                                  FPGA_MEASURE_STATUS_CLIP |
                                  FPGA_MEASURE_STATUS_OVERFLOW |
                                  FPGA_MEASURE_STATUS_PROTOCOL_ERROR)) == 0U) &&
               ((result->flags & (FPGA_MEASURE_STATUS_DONE |
                                  FPGA_MEASURE_STATUS_VALID)) ==
                                 (FPGA_MEASURE_STATUS_DONE |
                                  FPGA_MEASURE_STATUS_VALID)) &&
               (result->sample_count != 0UL) &&
               (result->adc_max > result->adc_min) &&
               (ratio <= 20UL)) ? 1U : 0U;

    OLED_Clear();
    OLED_Print(0U, 0U, "FPGA RX LOOP H=1");
    OLED_Print(0U, 1U, "RX:OK CRC:OK");
    OLED_Print(0U, 2U, "F:");
    OLED_PrintU32(2U, 2U, TEST_FREQUENCY_HZ);
    OLED_Print(6U, 2U, "HZ SEQ:");
    OLED_PrintU32(13U, 2U, result->sequence);
    OLED_Print(0U, 3U, "I:");
    OLED_PrintS64(2U, 3U, result->i_accumulator);
    OLED_Print(0U, 4U, "Q:");
    OLED_PrintS64(2U, 4U, result->q_accumulator);
    OLED_Print(0U, 5U, "N:");
    OLED_PrintU32(2U, 5U, result->sample_count);
    OLED_Print(0U, 6U, "ADC:");
    OLED_PrintU32(4U, 6U, result->adc_min);
    OLED_Print(7U, 6U, "-");
    OLED_PrintU32(8U, 6U, result->adc_max);
    OLED_Print(12U, 6U, "R:");
    OLED_PrintU32(14U, 6U, ratio);
    OLED_Print(17U, 6U, "%");
    OLED_Print(0U, 7U, status_text);
    OLED_Print(9U, 7U, "H1:");
    OLED_Print(12U, 7U, (h1_pass != 0U) ? "PASS" : "WARN");
    (void)OLED_Update();
}

static fpga_dds_status_t run_measurement(uint8_t sequence,
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
                                      TEST_MEASURE_CYCLES,
                                      TEST_SETTLE_TIME_MS);
    if (error != FPGA_DDS_OK) {
        return error;
    }

    while (elapsed < MEASURE_TIMEOUT_MS) {
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

int main(void)
{
    fpga_measure_result_t result;
    fpga_dds_status_t error;
    uint8_t sequence = 0U;
    uint8_t oled_ready;

    delay_init();
    FPGA_DDS_Init();
    delay_ms(300UL);
    oled_ready = OLED_Init();
    display_startup(oled_ready);

    error = FPGA_DDS_SetSine(TEST_FREQUENCY_HZ, TEST_AMPLITUDE_MVPP);
    if (error != FPGA_DDS_OK) {
        g_fpga_last_error = error;
        ++g_fpga_rx_error_count;
        display_error(oled_ready, error);
    }
    delay_ms(200UL);

    for (;;) {
        ++sequence;
        error = run_measurement(sequence, &result);
        g_fpga_last_error = error;

        if (error == FPGA_DDS_OK) {
            g_fpga_last_result = result;
            ++g_fpga_rx_ok_count;
            display_result(oled_ready, &result);
        } else {
            ++g_fpga_rx_error_count;
            display_error(oled_ready, error);
        }

        delay_ms(RESULT_DISPLAY_TIME_MS);
    }
}
