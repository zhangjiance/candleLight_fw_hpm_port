/*
 * timer.c - candleLight_fw's timer.h contract on HPMicro.
 *
 *   timer_init()  : remember the CPU frequency and the epoch
 *   timer_get()   : free running microsecond counter (32 bit, wraps ~71 min)
 *
 * Implemented on the RISC-V cycle counter (mcycle) so that no peripheral
 * timer is consumed and the value is safe to read from an interrupt.
 */

#include <stdint.h>

#include "hpm_soc.h"
#include "hpm_csr_drv.h"
#include "hpm_clock_drv.h"

#include "timer.h"
#include "hal_include.h"

static uint32_t s_cpu_freq = 0U;

void timer_init(void)
{
    s_cpu_freq = clock_get_frequency(clock_cpu0);
}

uint32_t timer_get(void)
{
    uint64_t cycle;
    uint64_t seconds;
    uint64_t remainder;

    if (s_cpu_freq == 0U) {
        return 0U;
    }

    cycle = hpm_csr_get_core_mcycle();

    /* Split so that the intermediate values cannot overflow 64 bit. */
    seconds = cycle / s_cpu_freq;
    remainder = cycle % s_cpu_freq;

    return (uint32_t)((seconds * 1000000ULL) + ((remainder * 1000000ULL) / s_cpu_freq));
}

uint32_t HAL_GetTick(void)
{
    if (s_cpu_freq == 0U) {
        return 0U;
    }

    return (uint32_t)(hpm_csr_get_core_mcycle() / (s_cpu_freq / 1000U));
}

void HAL_Delay(uint32_t ms)
{
    uint32_t start = HAL_GetTick();

    while ((uint32_t)(HAL_GetTick() - start) < ms) {
        ;
    }
}
