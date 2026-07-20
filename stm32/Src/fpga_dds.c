#include "fpga_dds.h"
#include "stm32f407_min.h"

#define FPGA_FRAME_SOF      0xA5U
#define FPGA_CMD_SET_SINE   0x01U
#define FPGA_FRAME_SIZE     11U

#define FPGA_CS_PIN         4U

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

static void fpga_cs_low(void)
{
    GPIOA_BSRR = (1UL << (FPGA_CS_PIN + 16U));
}

static void fpga_cs_high(void)
{
    GPIOA_BSRR = (1UL << FPGA_CS_PIN);
}

static uint8_t spi1_transfer(uint8_t value)
{
    while ((SPI1_SR & SPI_SR_TXE) == 0U) {
    }

    *(volatile uint8_t *)&SPI1_DR = value;

    while ((SPI1_SR & SPI_SR_RXNE) == 0U) {
    }

    return *(volatile uint8_t *)&SPI1_DR;
}

void FPGA_DDS_Init(void)
{
    volatile uint32_t dummy;

    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC_APB2ENR |= RCC_APB2ENR_SPI1EN;
    dummy = RCC_AHB1ENR;
    dummy = RCC_APB2ENR;
    (void)dummy;

    /* PA4: software chip-select output. PA5/PA7: SPI1 AF5. */
    GPIOA_MODER &= ~((3UL << (4U * 2U)) |
                     (3UL << (5U * 2U)) |
                     (3UL << (7U * 2U)));
    GPIOA_MODER |=  ((1UL << (4U * 2U)) |
                     (2UL << (5U * 2U)) |
                     (2UL << (7U * 2U)));

    GPIOA_OTYPER &= ~((1UL << 4U) | (1UL << 5U) | (1UL << 7U));
    GPIOA_OSPEEDR |= ((3UL << (4U * 2U)) |
                      (3UL << (5U * 2U)) |
                      (3UL << (7U * 2U)));
    GPIOA_PUPDR &= ~((3UL << (4U * 2U)) |
                     (3UL << (5U * 2U)) |
                     (3UL << (7U * 2U)));

    GPIOA_AFRL &= ~((0xFUL << (5U * 4U)) | (0xFUL << (7U * 4U)));
    GPIOA_AFRL |=  ((5UL << (5U * 4U)) | (5UL << (7U * 4U)));

    fpga_cs_high();

    /* SPI mode 0, 8-bit, MSB first, software NSS, HSI 16 MHz / 16 = 1 MHz. */
    SPI1_CR1 = 0U;
    SPI1_CR2 = 0U;
    SPI1_CR1 = SPI_CR1_MSTR |
               SPI_CR1_BR_DIV16 |
               SPI_CR1_SSM |
               SPI_CR1_SSI;
    SPI1_CR1 |= SPI_CR1_SPE;
}

uint64_t FPGA_DDS_FrequencyToFtw(uint32_t frequency_hz)
{
    /*
     * 2^48 / 125000000 = 2251799 remainder 101710656.
     * Splitting the quotient avoids overflowing a 64-bit intermediate.
     */
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
    uint8_t frame[FPGA_FRAME_SIZE];
    uint64_t ftw;
    uint16_t amplitude_q16;
    uint8_t crc = 0U;
    uint32_t index;

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

    for (index = 0U; index < FPGA_FRAME_SIZE - 1U; ++index) {
        crc = crc8_update(crc, frame[index]);
    }
    frame[10] = crc;

    fpga_cs_low();
    for (index = 0U; index < FPGA_FRAME_SIZE; ++index) {
        (void)spi1_transfer(frame[index]);
    }
    while ((SPI1_SR & SPI_SR_BSY) != 0U) {
    }
    fpga_cs_high();

    return FPGA_DDS_OK;
}
