#include <stdint.h>
#include "fpga_dds.h"

#define SWEEP_START_FREQUENCY_HZ  100UL
#define SWEEP_STOP_FREQUENCY_HZ   3000UL
#define SWEEP_STEP_FREQUENCY_HZ   100UL
#define SWEEP_DWELL_TIME_MS        2000UL

/* Keep the current test amplitude constant during the frequency sweep. */
#define SWEEP_AMPLITUDE_MVPP       3000UL

#define SYST_CSR                   (*(volatile uint32_t *)0xE000E010UL)
#define SYST_RVR                   (*(volatile uint32_t *)0xE000E014UL)
#define SYST_CVR                   (*(volatile uint32_t *)0xE000E018UL)
#define SYST_CSR_ENABLE            (1UL << 0)
#define SYST_CSR_CLKSOURCE         (1UL << 2)
#define SYST_CSR_COUNTFLAG         (1UL << 16)

extern uint32_t SystemCoreClock;

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
            /* Wait for one millisecond without enabling a SysTick interrupt. */
        }
        --milliseconds;
    }
}

int main(void)
{
    fpga_dds_status_t status;
    uint32_t sweep_frequency_hz = SWEEP_START_FREQUENCY_HZ;

    delay_init();
    FPGA_DDS_Init();
    delay_ms(300UL);

    for (;;) {
        status = FPGA_DDS_SetSine(sweep_frequency_hz,
                                  SWEEP_AMPLITUDE_MVPP);
        (void)status;

        delay_ms(SWEEP_DWELL_TIME_MS);

        if (sweep_frequency_hz >= SWEEP_STOP_FREQUENCY_HZ) {
            sweep_frequency_hz = SWEEP_START_FREQUENCY_HZ;
        } else {
            sweep_frequency_hz += SWEEP_STEP_FREQUENCY_HZ;
        }
    }
}
