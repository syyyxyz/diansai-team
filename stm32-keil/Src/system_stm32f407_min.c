#include <stdint.h>

uint32_t SystemCoreClock = 16000000UL;

void SystemInit(void)
{
    volatile uint32_t *cpacr = (volatile uint32_t *)0xE000ED88UL;
    volatile uint32_t *vtor  = (volatile uint32_t *)0xE000ED08UL;

    /* Enable the STM32F407 single-precision FPU for future DSP code. */
    *cpacr |= (0xFUL << 20);

    /* The linker places the vector table at the STM32 internal flash base. */
    *vtor = 0x08000000UL;

    /* Keep reset-default HSI = 16 MHz; the SPI prescaler assumes this clock. */
    SystemCoreClock = 16000000UL;
}

void SystemCoreClockUpdate(void)
{
    SystemCoreClock = 16000000UL;
}
