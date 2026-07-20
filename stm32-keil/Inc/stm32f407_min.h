#ifndef STM32F407_MIN_H
#define STM32F407_MIN_H

#include <stdint.h>

#define REG32(address) (*(volatile uint32_t *)(address))

#define RCC_BASE            0x40023800UL
#define GPIOA_BASE          0x40020000UL
#define GPIOB_BASE          0x40020400UL
#define I2C1_BASE           0x40005400UL
#define SPI1_BASE           0x40013000UL

#define RCC_AHB1ENR         REG32(RCC_BASE + 0x30UL)
#define RCC_APB1ENR         REG32(RCC_BASE + 0x40UL)
#define RCC_APB2ENR         REG32(RCC_BASE + 0x44UL)

#define GPIOA_MODER         REG32(GPIOA_BASE + 0x00UL)
#define GPIOA_OTYPER        REG32(GPIOA_BASE + 0x04UL)
#define GPIOA_OSPEEDR       REG32(GPIOA_BASE + 0x08UL)
#define GPIOA_PUPDR         REG32(GPIOA_BASE + 0x0CUL)
#define GPIOA_IDR           REG32(GPIOA_BASE + 0x10UL)
#define GPIOA_BSRR          REG32(GPIOA_BASE + 0x18UL)
#define GPIOA_AFRL          REG32(GPIOA_BASE + 0x20UL)

#define FLASH_BASE          0x40023C00UL
#define FLASH_KEYR          REG32(FLASH_BASE + 0x04UL)
#define FLASH_SR            REG32(FLASH_BASE + 0x0CUL)
#define FLASH_CR            REG32(FLASH_BASE + 0x10UL)

#define GPIOB_MODER         REG32(GPIOB_BASE + 0x00UL)
#define GPIOB_OTYPER        REG32(GPIOB_BASE + 0x04UL)
#define GPIOB_OSPEEDR       REG32(GPIOB_BASE + 0x08UL)
#define GPIOB_PUPDR         REG32(GPIOB_BASE + 0x0CUL)
#define GPIOB_IDR           REG32(GPIOB_BASE + 0x10UL)
#define GPIOB_BSRR          REG32(GPIOB_BASE + 0x18UL)
#define GPIOB_AFRL          REG32(GPIOB_BASE + 0x20UL)

#define I2C1_CR1            REG32(I2C1_BASE + 0x00UL)
#define I2C1_CR2            REG32(I2C1_BASE + 0x04UL)
#define I2C1_DR             REG32(I2C1_BASE + 0x10UL)
#define I2C1_SR1            REG32(I2C1_BASE + 0x14UL)
#define I2C1_SR2            REG32(I2C1_BASE + 0x18UL)
#define I2C1_CCR            REG32(I2C1_BASE + 0x1CUL)
#define I2C1_TRISE          REG32(I2C1_BASE + 0x20UL)

#define SPI1_CR1            REG32(SPI1_BASE + 0x00UL)
#define SPI1_CR2            REG32(SPI1_BASE + 0x04UL)
#define SPI1_SR             REG32(SPI1_BASE + 0x08UL)
#define SPI1_DR             REG32(SPI1_BASE + 0x0CUL)

#define RCC_AHB1ENR_GPIOAEN (1UL << 0)
#define RCC_AHB1ENR_GPIOBEN (1UL << 1)
#define RCC_APB1ENR_I2C1EN  (1UL << 21)
#define RCC_APB2ENR_SPI1EN  (1UL << 12)

#define I2C_CR1_PE          (1UL << 0)
#define I2C_CR1_START       (1UL << 8)
#define I2C_CR1_STOP        (1UL << 9)
#define I2C_CR1_SWRST       (1UL << 15)

#define I2C_SR1_SB          (1UL << 0)
#define I2C_SR1_ADDR        (1UL << 1)
#define I2C_SR1_BTF         (1UL << 2)
#define I2C_SR1_TXE         (1UL << 7)
#define I2C_SR1_AF          (1UL << 10)

#define I2C_SR2_BUSY        (1UL << 1)

#define SPI_CR1_CPHA        (1UL << 0)
#define SPI_CR1_CPOL        (1UL << 1)
#define SPI_CR1_MSTR        (1UL << 2)
#define SPI_CR1_BR_DIV16    (3UL << 3)
#define SPI_CR1_SPE         (1UL << 6)
#define SPI_CR1_SSI         (1UL << 8)
#define SPI_CR1_SSM         (1UL << 9)

#define SPI_SR_RXNE         (1UL << 0)
#define SPI_SR_TXE          (1UL << 1)
#define SPI_SR_BSY          (1UL << 7)

#define FLASH_SR_EOP        (1UL << 0)
#define FLASH_SR_OPERR      (1UL << 1)
#define FLASH_SR_WRPERR     (1UL << 4)
#define FLASH_SR_PGAERR     (1UL << 5)
#define FLASH_SR_PGPERR     (1UL << 6)
#define FLASH_SR_PGSERR     (1UL << 7)
#define FLASH_SR_BSY        (1UL << 16)

#define FLASH_CR_PG         (1UL << 0)
#define FLASH_CR_SER        (1UL << 1)
#define FLASH_CR_SNB_SHIFT  3U
#define FLASH_CR_PSIZE_WORD (2UL << 8)
#define FLASH_CR_STRT       (1UL << 16)
#define FLASH_CR_LOCK       (1UL << 31)

#endif
