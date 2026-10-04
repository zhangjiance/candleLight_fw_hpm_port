/*
 * cmsis_device.h - RISC-V (HPMicro) replacement for candleLight_fw's CMSIS dependency.
 *
 * candleLight_fw's util.h only needs a handful of Cortex-M intrinsics:
 *   __get_PRIMASK() / __disable_irq() / __enable_irq() / __ISB()
 * This header implements them on top of the RISC-V mstatus.MIE bit so that
 * candleLight's util.h (and therefore can_common.c / usbd_gs_can.c) can be
 * compiled unmodified.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "hpm_common.h"
#include "hpm_soc.h"

#define HPM_MSTATUS_MIE_BIT (0x8UL) /* mstatus.MIE == bit 3 */

static inline uint32_t hpm_csr_read_mstatus(void)
{
    uint32_t v;
    __asm volatile("csrr %0, mstatus" : "=r"(v));
    return v;
}

static inline void hpm_csr_set_mstatus_mie(void)
{
    __asm volatile("csrs mstatus, %0" ::"r"(HPM_MSTATUS_MIE_BIT) : "memory");
}

static inline void hpm_csr_clear_mstatus_mie(void)
{
    __asm volatile("csrc mstatus, %0" ::"r"(HPM_MSTATUS_MIE_BIT) : "memory");
}

/* --- CMSIS intrinsic names, as used by candleLight_fw/include/util.h --- */

static inline uint32_t __get_PRIMASK(void)
{
    /* 0 == interrupts enabled, 1 == interrupts masked (Cortex-M semantics) */
    return (hpm_csr_read_mstatus() & HPM_MSTATUS_MIE_BIT) ? 0U : 1U;
}

static inline void __disable_irq(void)
{
    hpm_csr_clear_mstatus_mie();
}

static inline void __enable_irq(void)
{
    hpm_csr_set_mstatus_mie();
}

static inline void __ISB(void)
{
    __asm volatile("fence.i" ::: "memory");
}

static inline void __DSB(void)
{
    __asm volatile("fence" ::: "memory");
}

static inline void __DMB(void)
{
    __asm volatile("fence" ::: "memory");
}

/* candleLight_fw/src/util.c uses __BKPT(0) inside assert_failed() */
static inline void __BKPT(uint8_t val)
{
    (void)val;
    __asm volatile("ebreak");
}

/* Cortex-M software reset equivalent (kept for API compatibility) */
void NVIC_SystemReset(void);
