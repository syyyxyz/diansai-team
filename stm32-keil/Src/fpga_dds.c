#include "fpga_dds.h"
#include "stm32f407_min.h"

#define FPGA_FRAME_SOF          0xA5U
#define FPGA_CMD_SET_SINE       0x01U
#define FPGA_CMD_START          0x02U
#define FPGA_CMD_READ_STATUS    0x03U
#define FPGA_CMD_READ_RESULT    0x04U
#define FPGA_CMD_CLEAR_RESULT   0x05U
#define FPGA_RSP_STATUS         0x83U
#define FPGA_RSP_RESULT         0x84U

#define FPGA_SET_SINE_SIZE      11U
#define FPGA_START_SIZE          8U
#define FPGA_REQUEST_SIZE        3U
#define FPGA_STATUS_SIZE         5U
#define FPGA_RESULT_SIZE        33U

#define FPGA_CS_PIN              4U
#define FPGA_SPI_TIMEOUT_COUNT   100000UL
#define FPGA_RESPONSE_GAP_COUNT  128UL

static uint8_t crc8_update(uint8_t crc, uint8_t data)
{
    uint32_t bit;

    for (bit = 0U; bit < 8U; ++bit) {
        if (((crc ^ data) & 0x80U) != 0U) {
            crc = (uint8_t)((crc << 1) ^ 0x07U);
        } else {
            crc = (uint8_t)(crc << 1);
        }
        data = (uint8_t)(data << 1);
    }

    return crc;
}

static uint8_t frame_crc(const uint8_t *frame, uint32_t length)
{
    uint8_t crc = 0U;
    uint32_t index;

    for (index = 0U; index < length; ++index) {
        crc = crc8_update(crc, frame[index]);
    }
    return crc;
}

static void fpga_cs_low(void)
{
    GPIOA_BSRR = (1UL << (FPGA_CS_PIN + 16U));
}

static void fpga_cs_high(void)
{
    GPIOA_BSRR = (1UL << FPGA_CS_PIN);
}

static void fpga_protocol_gap(void)
{
    volatile uint32_t count;

    /* Gives the 125 MHz FPGA enough time to synchronize CS and build CRC. */
    for (count = 0U; count < FPGA_RESPONSE_GAP_COUNT; ++count) {
    }
}

static fpga_dds_status_t spi1_transfer(uint8_t value, uint8_t *received)
{
    uint32_t timeout = FPGA_SPI_TIMEOUT_COUNT;

    while ((SPI1_SR & SPI_SR_TXE) == 0U) {
        if (--timeout == 0U) {
            return FPGA_DDS_ERROR_SPI_TIMEOUT;
        }
    }

    *(volatile uint8_t *)&SPI1_DR = value;
    timeout = FPGA_SPI_TIMEOUT_COUNT;

    while ((SPI1_SR & SPI_SR_RXNE) == 0U) {
        if (--timeout == 0U) {
            return FPGA_DDS_ERROR_SPI_TIMEOUT;
        }
    }

    *received = *(volatile uint8_t *)&SPI1_DR;
    return FPGA_DDS_OK;
}

static fpga_dds_status_t spi_transaction(const uint8_t *transmit,
                                         uint8_t *receive,
                                         uint32_t length)
{
    fpga_dds_status_t result = FPGA_DDS_OK;
    uint8_t discarded;
    uint32_t index;
    uint32_t timeout;

    fpga_cs_low();
    for (index = 0U; index < length; ++index) {
        result = spi1_transfer((transmit != 0) ? transmit[index] : 0U,
                               (receive != 0) ? &receive[index] : &discarded);
        if (result != FPGA_DDS_OK) {
            break;
        }
    }

    timeout = FPGA_SPI_TIMEOUT_COUNT;
    while ((SPI1_SR & SPI_SR_BSY) != 0U) {
        if (--timeout == 0U) {
            result = FPGA_DDS_ERROR_SPI_TIMEOUT;
            break;
        }
    }
    fpga_cs_high();
    fpga_protocol_gap();
    return result;
}

static fpga_dds_status_t request_response(uint8_t command,
                                          uint8_t *response,
                                          uint32_t response_length)
{
    uint8_t request[FPGA_REQUEST_SIZE];
    fpga_dds_status_t status;

    request[0] = FPGA_FRAME_SOF;
    request[1] = command;
    request[2] = frame_crc(request, 2U);

    status = spi_transaction(request, 0, FPGA_REQUEST_SIZE);
    if (status != FPGA_DDS_OK) {
        return status;
    }

    return spi_transaction(0, response, response_length);
}

static uint64_t read_be_u64(const uint8_t *data)
{
    uint64_t value = 0ULL;
    uint32_t index;

    for (index = 0U; index < 8U; ++index) {
        value = (value << 8) | data[index];
    }
    return value;
}

void FPGA_DDS_Init(void)
{
    volatile uint32_t dummy;

    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC_APB2ENR |= RCC_APB2ENR_SPI1EN;
    dummy = RCC_AHB1ENR;
    dummy = RCC_APB2ENR;
    (void)dummy;

    /* PA4=CS, PA5=SCK, PA6=MISO and PA7=MOSI (SPI1 AF5). */
    GPIOA_MODER &= ~((3UL << (4U * 2U)) |
                     (3UL << (5U * 2U)) |
                     (3UL << (6U * 2U)) |
                     (3UL << (7U * 2U)));
    GPIOA_MODER |=  ((1UL << (4U * 2U)) |
                     (2UL << (5U * 2U)) |
                     (2UL << (6U * 2U)) |
                     (2UL << (7U * 2U)));

    GPIOA_OTYPER &= ~((1UL << 4U) | (1UL << 5U) |
                      (1UL << 6U) | (1UL << 7U));
    GPIOA_OSPEEDR |= ((3UL << (4U * 2U)) |
                      (3UL << (5U * 2U)) |
                      (3UL << (6U * 2U)) |
                      (3UL << (7U * 2U)));
    GPIOA_PUPDR &= ~((3UL << (4U * 2U)) |
                     (3UL << (5U * 2U)) |
                     (3UL << (6U * 2U)) |
                     (3UL << (7U * 2U)));
    /* A disconnected MISO reads zero instead of random valid-looking data. */
    GPIOA_PUPDR |= (2UL << (6U * 2U));

    GPIOA_AFRL &= ~((0xFUL << (5U * 4U)) |
                    (0xFUL << (6U * 4U)) |
                    (0xFUL << (7U * 4U)));
    GPIOA_AFRL |=  ((5UL << (5U * 4U)) |
                    (5UL << (6U * 4U)) |
                    (5UL << (7U * 4U)));

    fpga_cs_high();

    /* SPI mode 0, 8-bit, MSB first, HSI 16 MHz / 16 = 1 MHz. */
    SPI1_CR1 = 0U;
    SPI1_CR2 = 0U;
    SPI1_CR1 = SPI_CR1_MSTR |
               SPI_CR1_BR_DIV16 |
               SPI_CR1_SSM |
               SPI_CR1_SSI;
    SPI1_CR1 |= SPI_CR1_SPE;
    fpga_protocol_gap();
}

uint64_t FPGA_DDS_FrequencyToFtw(uint32_t frequency_hz)
{
    const uint64_t quotient = 2251799ULL;
    const uint64_t remainder = 101710656ULL;
    const uint64_t divisor = FPGA_DDS_SAMPLE_CLOCK_HZ;
    uint64_t frequency = frequency_hz;

    return frequency * quotient +
           (frequency * remainder + divisor / 2ULL) / divisor;
}

uint16_t FPGA_DDS_AmplitudeToQ16(uint32_t amplitude_mvpp)
{
    if (amplitude_mvpp >= FPGA_DDS_FULL_SCALE_MVPP) {
        return 65535U;
    }

    return (uint16_t)((amplitude_mvpp * 65535UL +
                       FPGA_DDS_FULL_SCALE_MVPP / 2UL) /
                      FPGA_DDS_FULL_SCALE_MVPP);
}

fpga_dds_status_t FPGA_DDS_SetSine(uint32_t frequency_hz,
                                  uint32_t amplitude_mvpp)
{
    uint8_t frame[FPGA_SET_SINE_SIZE];
    uint64_t ftw;
    uint16_t amplitude_q16;

    if ((frequency_hz < FPGA_DDS_MIN_FREQUENCY_HZ) ||
        (frequency_hz > FPGA_DDS_MAX_FREQUENCY_HZ)) {
        return FPGA_DDS_ERROR_FREQUENCY;
    }
    if (amplitude_mvpp > FPGA_DDS_FULL_SCALE_MVPP) {
        return FPGA_DDS_ERROR_AMPLITUDE;
    }

    ftw = FPGA_DDS_FrequencyToFtw(frequency_hz);
    amplitude_q16 = FPGA_DDS_AmplitudeToQ16(amplitude_mvpp);

    frame[0] = FPGA_FRAME_SOF;
    frame[1] = FPGA_CMD_SET_SINE;
    frame[2] = (uint8_t)(ftw >> 40);
    frame[3] = (uint8_t)(ftw >> 32);
    frame[4] = (uint8_t)(ftw >> 24);
    frame[5] = (uint8_t)(ftw >> 16);
    frame[6] = (uint8_t)(ftw >> 8);
    frame[7] = (uint8_t)ftw;
    frame[8] = (uint8_t)(amplitude_q16 >> 8);
    frame[9] = (uint8_t)amplitude_q16;
    frame[10] = frame_crc(frame, FPGA_SET_SINE_SIZE - 1U);

    return spi_transaction(frame, 0, FPGA_SET_SINE_SIZE);
}

fpga_dds_status_t FPGA_DDS_StartMeasurement(uint8_t sequence,
                                            uint16_t cycles,
                                            uint16_t settle_ms)
{
    uint8_t frame[FPGA_START_SIZE];

    if (cycles == 0U) {
        return FPGA_DDS_ERROR_ARGUMENT;
    }

    frame[0] = FPGA_FRAME_SOF;
    frame[1] = FPGA_CMD_START;
    frame[2] = sequence;
    frame[3] = (uint8_t)(cycles >> 8);
    frame[4] = (uint8_t)cycles;
    frame[5] = (uint8_t)(settle_ms >> 8);
    frame[6] = (uint8_t)settle_ms;
    frame[7] = frame_crc(frame, FPGA_START_SIZE - 1U);

    return spi_transaction(frame, 0, FPGA_START_SIZE);
}

fpga_dds_status_t FPGA_DDS_ReadStatus(fpga_measure_status_t *status)
{
    uint8_t response[FPGA_STATUS_SIZE];
    fpga_dds_status_t result;

    if (status == 0) {
        return FPGA_DDS_ERROR_ARGUMENT;
    }

    result = request_response(FPGA_CMD_READ_STATUS,
                              response, FPGA_STATUS_SIZE);
    if (result != FPGA_DDS_OK) {
        return result;
    }
    if ((response[0] != FPGA_FRAME_SOF) ||
        (response[1] != FPGA_RSP_STATUS)) {
        return FPGA_DDS_ERROR_FRAME;
    }
    if (frame_crc(response, FPGA_STATUS_SIZE - 1U) != response[4]) {
        return FPGA_DDS_ERROR_CRC;
    }

    status->sequence = response[2];
    status->flags = response[3];
    return FPGA_DDS_OK;
}

fpga_dds_status_t FPGA_DDS_ReadResult(uint8_t expected_sequence,
                                     fpga_measure_result_t *result)
{
    uint8_t response[FPGA_RESULT_SIZE];
    fpga_dds_status_t status;
    uint64_t frequency_word = 0ULL;
    uint32_t index;

    if (result == 0) {
        return FPGA_DDS_ERROR_ARGUMENT;
    }

    status = request_response(FPGA_CMD_READ_RESULT,
                              response, FPGA_RESULT_SIZE);
    if (status != FPGA_DDS_OK) {
        return status;
    }
    if ((response[0] != FPGA_FRAME_SOF) ||
        (response[1] != FPGA_RSP_RESULT)) {
        return FPGA_DDS_ERROR_FRAME;
    }
    if (frame_crc(response, FPGA_RESULT_SIZE - 1U) != response[32]) {
        return FPGA_DDS_ERROR_CRC;
    }
    if (response[2] != expected_sequence) {
        return FPGA_DDS_ERROR_SEQUENCE;
    }

    for (index = 0U; index < 6U; ++index) {
        frequency_word = (frequency_word << 8) | response[3U + index];
    }

    result->sequence = response[2];
    result->frequency_word = frequency_word;
    result->i_accumulator = (int64_t)read_be_u64(&response[9]);
    result->q_accumulator = (int64_t)read_be_u64(&response[17]);
    result->sample_count = ((uint32_t)response[25] << 24) |
                           ((uint32_t)response[26] << 16) |
                           ((uint32_t)response[27] << 8) |
                           response[28];
    result->adc_min = response[29];
    result->adc_max = response[30];
    result->flags = response[31];
    return FPGA_DDS_OK;
}

fpga_dds_status_t FPGA_DDS_ClearResult(void)
{
    uint8_t frame[FPGA_REQUEST_SIZE];

    frame[0] = FPGA_FRAME_SOF;
    frame[1] = FPGA_CMD_CLEAR_RESULT;
    frame[2] = frame_crc(frame, FPGA_REQUEST_SIZE - 1U);
    return spi_transaction(frame, 0, FPGA_REQUEST_SIZE);
}
