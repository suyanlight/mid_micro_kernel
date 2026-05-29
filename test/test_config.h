/******************************************************************************
 * @file    test_config.h
 * @brief   Test-mode overrides for host-based unit testing.
 *
 * @details
 * On a host (x86_64 / ARM64 Linux), there are no Cortex-M PRIMASK/WFI/DMB
 * instructions. This file overrides the hardware abstraction macros AFTER
 * including the real config, so unit tests compile and run natively.
 *
 * Usage:
 *   #include "test_config.h"   // instead of "mid_micro_kernel_config.h"
 *****************************************************************************/

#ifndef TEST_CONFIG_H
#define TEST_CONFIG_H

#include "../src/mid_micro_kernel_config.h"

/* ── Override hardware macros for host testing ── */
#undef  MK_WFI
#define MK_WFI()                          ((void)0)

#undef  MK_DISABLE_IRQ
#define MK_DISABLE_IRQ()                  ((void)0)

#undef  MK_ENABLE_IRQ
#define MK_ENABLE_IRQ()                   ((void)0)

#undef  MK_GET_PRIMASK
#define MK_GET_PRIMASK()                  0U

#undef  MK_DATA_MB
#define MK_DATA_MB()                      ((void)0)

/* Trace irq-enable/disable calls to verify critical section coverage.
 * If a test fails with "unexpected irq state", some operation left irq
 * disabled or the test expected them disabled. */
static int s_irq_depth = 0;
#undef  MK_DISABLE_IRQ
#define MK_DISABLE_IRQ()                  do { s_irq_depth++; } while(0)
#undef  MK_ENABLE_IRQ
#define MK_ENABLE_IRQ()                   do { s_irq_depth--; } while(0)

/* Provide a non-CLZ fallback for host-tested MK_GET_HIGHEST_PRIO.
 * The config.h fallback already does this for non-GCC/Clang compilers,
 * but on Linux host we are running GCC — we force the software CLZ
 * path here for deterministic test results. */
#undef  MK_GET_HIGHEST_PRIO
static inline uint8_t test_soft_clz(uint32_t map) {
    uint8_t pos = 0;
    while (map >>= 1) pos++;
    return pos;
}
#define MK_GET_HIGHEST_PRIO(map)          test_soft_clz(map)

#endif /* TEST_CONFIG_H */
