#include <stddef.h>

#include "calibration_store.h"
#include "stm32f407_min.h"

#define CALIBRATION_FLASH_ADDRESS 0x08060000UL
#define CALIBRATION_FLASH_SECTOR  7UL
#define FLASH_WAIT_LIMIT          8000000UL
#define FLASH_ERROR_MASK          (FLASH_SR_OPERR | FLASH_SR_WRPERR | \
                                   FLASH_SR_PGAERR | FLASH_SR_PGPERR | \
                                   FLASH_SR_PGSERR)

typedef char calibration_layout_check[
    (offsetof(calibration_table_t, crc32) == 976U) ? 1 : -1];

static uint32_t crc32_bytes(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t index;
    uint32_t bit;

    for (index = 0U; index < length; ++index) {
        crc ^= data[index];
        for (bit = 0U; bit < 8U; ++bit) {
            crc = ((crc & 1UL) != 0UL) ?
                  (crc >> 1) ^ 0xEDB88320UL : crc >> 1;
        }
    }
    return ~crc;
}

static uint8_t flash_wait(void)
{
    uint32_t timeout = FLASH_WAIT_LIMIT;

    while ((FLASH_SR & FLASH_SR_BSY) != 0UL) {
        if (--timeout == 0UL) {
            return 0U;
        }
    }
    return ((FLASH_SR & FLASH_ERROR_MASK) == 0UL) ? 1U : 0U;
}

static uint8_t flash_unlock(void)
{
    if ((FLASH_CR & FLASH_CR_LOCK) != 0UL) {
        FLASH_KEYR = 0x45670123UL;
        FLASH_KEYR = 0xCDEF89ABUL;
    }
    return ((FLASH_CR & FLASH_CR_LOCK) == 0UL) ? 1U : 0U;
}

static void flash_lock(void)
{
    FLASH_CR = FLASH_CR_LOCK;
}

uint8_t CalibrationStore_Load(calibration_table_t *table)
{
    const volatile uint32_t *source =
        (const volatile uint32_t *)CALIBRATION_FLASH_ADDRESS;
    uint32_t *destination = (uint32_t *)table;
    const uint32_t stored_bytes =
        (uint32_t)offsetof(calibration_table_t, crc32) + sizeof(uint32_t);
    uint32_t index;

    if (table == 0) {
        return 0U;
    }

    for (index = 0U; index < stored_bytes / sizeof(uint32_t); ++index) {
        destination[index] = source[index];
    }

    if ((table->magic != CALIBRATION_MAGIC) ||
        (table->version != CALIBRATION_VERSION) ||
        (table->point_count != MODEL_POINT_COUNT) ||
        (table->crc32 != crc32_bytes((const uint8_t *)table,
                                     (uint32_t)offsetof(calibration_table_t,
                                                        crc32)))) {
        return 0U;
    }

    for (index = 0U; index < MODEL_POINT_COUNT; ++index) {
        if ((table->points[index].frequency_hz != Model_FrequencyAt(index)) ||
            (table->points[index].sample_count == 0UL) ||
            (table->points[index].amplitude_mvpp == 0UL)) {
            return 0U;
        }
    }
    return 1U;
}

uint8_t CalibrationStore_Erase(void)
{
    uint8_t result;

    if ((flash_wait() == 0U) || (flash_unlock() == 0U)) {
        return 0U;
    }

    FLASH_SR = FLASH_SR_EOP | FLASH_ERROR_MASK;
    FLASH_CR = FLASH_CR_PSIZE_WORD | FLASH_CR_SER |
               (CALIBRATION_FLASH_SECTOR << FLASH_CR_SNB_SHIFT);
    FLASH_CR |= FLASH_CR_STRT;
    result = flash_wait();
    FLASH_CR = 0UL;
    flash_lock();
    return result;
}

uint8_t CalibrationStore_Save(calibration_table_t *table)
{
    volatile uint32_t *destination =
        (volatile uint32_t *)CALIBRATION_FLASH_ADDRESS;
    const uint32_t *source;
    const uint32_t stored_bytes =
        (uint32_t)offsetof(calibration_table_t, crc32) + sizeof(uint32_t);
    uint32_t index;
    uint8_t result = 1U;

    if (table == 0) {
        return 0U;
    }

    table->magic = CALIBRATION_MAGIC;
    table->version = CALIBRATION_VERSION;
    table->point_count = MODEL_POINT_COUNT;
    table->reserved = 0UL;
    table->crc32 = crc32_bytes((const uint8_t *)table,
                               (uint32_t)offsetof(calibration_table_t, crc32));

    if (CalibrationStore_Erase() == 0U) {
        return 0U;
    }
    if (flash_unlock() == 0U) {
        return 0U;
    }

    FLASH_SR = FLASH_SR_EOP | FLASH_ERROR_MASK;
    FLASH_CR = FLASH_CR_PSIZE_WORD | FLASH_CR_PG;
    source = (const uint32_t *)table;
    for (index = 0U; index < stored_bytes / sizeof(uint32_t); ++index) {
        destination[index] = source[index];
        if ((flash_wait() == 0U) || (destination[index] != source[index])) {
            result = 0U;
            break;
        }
    }
    FLASH_CR = 0UL;
    flash_lock();

    return result;
}
