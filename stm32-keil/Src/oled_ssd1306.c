#include "oled_ssd1306.h"
#include "stm32f407_min.h"

#define OLED_I2C_ADDRESS       0x78U
#define OLED_COLUMNS_PER_CHAR  6U
#define OLED_PAGE_COUNT        8U
#define I2C_TIMEOUT_COUNT      100000UL

typedef struct {
    char character;
    uint8_t columns[5];
} glyph_t;

/* Compact 5x7 font: digits, punctuation and uppercase letters used by UI. */
static const glyph_t font[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'%', {0x63, 0x13, 0x08, 0x64, 0x63}},
    {'+', {0x08, 0x08, 0x3E, 0x08, 0x08}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {'/', {0x20, 0x10, 0x08, 0x04, 0x02}},
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
    {':', {0x00, 0x36, 0x36, 0x00, 0x00}},
    {'=', {0x14, 0x14, 0x14, 0x14, 0x14}},
    {'?', {0x02, 0x01, 0x51, 0x09, 0x06}},
    {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
    {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}},
    {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
    {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
    {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
    {'Y', {0x07, 0x08, 0x70, 0x08, 0x07}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}}
};

static uint8_t framebuffer[OLED_WIDTH * OLED_PAGE_COUNT];

static uint8_t i2c_wait_sr1(uint32_t mask)
{
    uint32_t timeout = I2C_TIMEOUT_COUNT;

    while ((I2C1_SR1 & mask) == 0U) {
        if ((I2C1_SR1 & I2C_SR1_AF) != 0U) {
            I2C1_SR1 &= ~I2C_SR1_AF;
            return 0U;
        }
        if (--timeout == 0U) {
            return 0U;
        }
    }
    return 1U;
}

static void i2c_peripheral_init(void)
{
    I2C1_CR1 = I2C_CR1_SWRST;
    I2C1_CR1 = 0U;
    /* APB1 is 16 MHz: standard-mode 100 kHz timing. */
    I2C1_CR2 = 16U;
    I2C1_CCR = 80U;
    I2C1_TRISE = 17U;
    I2C1_CR1 = I2C_CR1_PE;
}

static void i2c_recover(void)
{
    volatile uint32_t delay;
    uint32_t pulse;

    I2C1_CR1 &= ~I2C_CR1_PE;
    GPIOB_MODER &= ~((3UL << (6U * 2U)) | (3UL << (7U * 2U)));
    GPIOB_MODER |=  ((1UL << (6U * 2U)) | (1UL << (7U * 2U)));
    GPIOB_OTYPER |= (1UL << 6U) | (1UL << 7U);
    GPIOB_BSRR = (1UL << 6U) | (1UL << 7U);

    for (pulse = 0U; pulse < 9U; ++pulse) {
        GPIOB_BSRR = (1UL << (6U + 16U));
        for (delay = 0U; delay < 40U; ++delay) {
        }
        GPIOB_BSRR = (1UL << 6U);
        for (delay = 0U; delay < 40U; ++delay) {
        }
    }

    GPIOB_BSRR = (1UL << (7U + 16U));
    GPIOB_BSRR = (1UL << 6U);
    for (delay = 0U; delay < 40U; ++delay) {
    }
    GPIOB_BSRR = (1UL << 7U);

    GPIOB_MODER &= ~((3UL << (6U * 2U)) | (3UL << (7U * 2U)));
    GPIOB_MODER |=  ((2UL << (6U * 2U)) | (2UL << (7U * 2U)));
    i2c_peripheral_init();
}

static uint8_t i2c_begin(void)
{
    uint32_t timeout = I2C_TIMEOUT_COUNT;
    volatile uint32_t clear;

    while ((I2C1_SR2 & I2C_SR2_BUSY) != 0U) {
        if (--timeout == 0U) {
            i2c_recover();
            if ((I2C1_SR2 & I2C_SR2_BUSY) != 0U) {
                return 0U;
            }
            break;
        }
    }

    I2C1_CR1 |= I2C_CR1_START;
    if (i2c_wait_sr1(I2C_SR1_SB) == 0U) {
        return 0U;
    }
    I2C1_DR = OLED_I2C_ADDRESS;
    if (i2c_wait_sr1(I2C_SR1_ADDR) == 0U) {
        I2C1_CR1 |= I2C_CR1_STOP;
        return 0U;
    }
    clear = I2C1_SR1;
    clear = I2C1_SR2;
    (void)clear;
    return 1U;
}

static uint8_t i2c_write(uint8_t value)
{
    if (i2c_wait_sr1(I2C_SR1_TXE) == 0U) {
        return 0U;
    }
    I2C1_DR = value;
    return 1U;
}

static uint8_t i2c_end(void)
{
    uint8_t result = i2c_wait_sr1(I2C_SR1_BTF);
    I2C1_CR1 |= I2C_CR1_STOP;
    return result;
}

static uint8_t oled_command(uint8_t command)
{
    if ((i2c_begin() == 0U) ||
        (i2c_write(0x00U) == 0U) ||
        (i2c_write(command) == 0U)) {
        I2C1_CR1 |= I2C_CR1_STOP;
        return 0U;
    }
    return i2c_end();
}

static const uint8_t *find_glyph(char character)
{
    uint32_t index;

    for (index = 0U; index < (sizeof(font) / sizeof(font[0])); ++index) {
        if (font[index].character == character) {
            return font[index].columns;
        }
    }
    return font[18].columns; /* '?' */
}

static void oled_put_character(uint8_t column, uint8_t row, char character)
{
    const uint8_t *glyph;
    uint32_t pixel_column;
    uint32_t offset;

    if ((column >= (OLED_WIDTH / OLED_COLUMNS_PER_CHAR)) ||
        (row >= OLED_PAGE_COUNT)) {
        return;
    }

    glyph = find_glyph(character);
    offset = (uint32_t)row * OLED_WIDTH +
             (uint32_t)column * OLED_COLUMNS_PER_CHAR;
    for (pixel_column = 0U; pixel_column < 5U; ++pixel_column) {
        framebuffer[offset + pixel_column] = glyph[pixel_column];
    }
    framebuffer[offset + 5U] = 0U;
}

uint8_t OLED_Init(void)
{
    static const uint8_t init_commands[] = {
        0xAEU, 0xD5U, 0x80U, 0xA8U, 0x3FU, 0xD3U, 0x00U, 0x40U,
        0xA1U, 0xC8U, 0xDAU, 0x12U, 0x81U, 0xCFU, 0xD9U, 0xF1U,
        0xDBU, 0x30U, 0xA4U, 0xA6U, 0x8DU, 0x14U, 0xAFU
    };
    volatile uint32_t dummy;
    uint32_t index;

    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    RCC_APB1ENR |= RCC_APB1ENR_I2C1EN;
    dummy = RCC_AHB1ENR;
    dummy = RCC_APB1ENR;
    (void)dummy;

    GPIOB_MODER &= ~((3UL << (6U * 2U)) | (3UL << (7U * 2U)));
    GPIOB_MODER |=  ((2UL << (6U * 2U)) | (2UL << (7U * 2U)));
    GPIOB_OTYPER |= (1UL << 6U) | (1UL << 7U);
    GPIOB_OSPEEDR |= (3UL << (6U * 2U)) | (3UL << (7U * 2U));
    GPIOB_PUPDR &= ~((3UL << (6U * 2U)) | (3UL << (7U * 2U)));
    GPIOB_PUPDR |=  (1UL << (6U * 2U)) | (1UL << (7U * 2U));
    GPIOB_AFRL &= ~((0xFUL << (6U * 4U)) | (0xFUL << (7U * 4U)));
    GPIOB_AFRL |=  (4UL << (6U * 4U)) | (4UL << (7U * 4U));

    i2c_peripheral_init();
    for (index = 0U; index < sizeof(init_commands); ++index) {
        if (oled_command(init_commands[index]) == 0U) {
            return 0U;
        }
    }

    OLED_Clear();
    return OLED_Update();
}

void OLED_Clear(void)
{
    uint32_t index;

    for (index = 0U; index < sizeof(framebuffer); ++index) {
        framebuffer[index] = 0U;
    }
}

void OLED_Print(uint8_t column, uint8_t row, const char *text)
{
    if (text == 0) {
        return;
    }
    while ((*text != '\0') &&
           (column < (OLED_WIDTH / OLED_COLUMNS_PER_CHAR))) {
        oled_put_character(column++, row, *text++);
    }
}

void OLED_PrintU64(uint8_t column, uint8_t row, uint64_t value)
{
    char buffer[21];
    uint32_t position = sizeof(buffer);

    buffer[--position] = '\0';
    do {
        buffer[--position] = (char)('0' + (value % 10ULL));
        value /= 10ULL;
    } while ((value != 0ULL) && (position != 0U));
    OLED_Print(column, row, &buffer[position]);
}

void OLED_PrintU32(uint8_t column, uint8_t row, uint32_t value)
{
    OLED_PrintU64(column, row, value);
}

void OLED_PrintS64(uint8_t column, uint8_t row, int64_t value)
{
    uint64_t magnitude;

    if (value < 0) {
        oled_put_character(column++, row, '-');
        magnitude = (uint64_t)(-(value + 1)) + 1ULL;
    } else {
        oled_put_character(column++, row, '+');
        magnitude = (uint64_t)value;
    }
    OLED_PrintU64(column, row, magnitude);
}

uint8_t OLED_Update(void)
{
    uint32_t page;
    uint32_t column;

    for (page = 0U; page < OLED_PAGE_COUNT; ++page) {
        if ((oled_command((uint8_t)(0xB0U | page)) == 0U) ||
            (oled_command(0x00U) == 0U) ||
            (oled_command(0x10U) == 0U)) {
            return 0U;
        }

        if ((i2c_begin() == 0U) || (i2c_write(0x40U) == 0U)) {
            I2C1_CR1 |= I2C_CR1_STOP;
            return 0U;
        }
        for (column = 0U; column < OLED_WIDTH; ++column) {
            if (i2c_write(framebuffer[page * OLED_WIDTH + column]) == 0U) {
                I2C1_CR1 |= I2C_CR1_STOP;
                return 0U;
            }
        }
        if (i2c_end() == 0U) {
            return 0U;
        }
    }
    return 1U;
}
