/******************************************************************************
 * @file    mid_micro_kernel_config.h
 * @brief   Micro-Kernel Config and Signal Identification Definitions.
 *
 * Detailed Description:
 * - Architecture Role: This configuration header acts as the static firmware
 * configuration file for the micro-kernel. It determines the core scheduling
 * primitive (Queue vs. Bitmap) and reserves global event spaces.
 *
 * - Signaling Layer: Enforces strict encapsulation of event identifiers. 
 * Signals 0x00 to 0x0F are reserved for system-level execution, whereas 
 * 0x10 and above are allocated for custom application layer active objects.
 *
 * - Portability Layer (v2.0): Extended with cross-compiler WFI / PRIMASK / DMB
 * macros, enabling low-power idle and ISR-safe multi-producer enqueue across
 * GCC, Clang, and Keil ARMCC toolchains.
 *
 * @note When to prefer v1.x over v2.0:
 *   v1.x single global FIFO queue is a better fit when (a) RAM is extremely tight
 *   and per-AO queue overhead is unacceptable, (b) you have only ONE interrupt
 *   source posting events (SPSC volatile is sufficient; PRIMASK critical section
 *   is unnecessary overhead), or (c) you depend on strict global FIFO ordering
 *   across ALL event sources — v2.0 round-robin per-AO dispatch reorders events.
 *
 * Version History:
 * - 2.0.0 (2026-05-28):
 *   - Added MK_AO_QUEUE_CAPACITY (per-AO queue depth, default 8).
 *   - Added MK_TIMER_MAX (software timer pool size, default 16).
 *   - Added MK_WFI / MK_DISABLE_IRQ / MK_ENABLE_IRQ / MK_DATA_MB hardware
 *     abstraction layer for GCC/Clang/ARMCC.
 * - 1.0.0 (2026-02-04): Initial release.
 *
 * Author:   suyan
 * Date:     2026-05-28
 * Version:  2.0.0
 *****************************************************************************/
#ifndef MID_MICRO_KERNEL_CONFIG_H
#define MID_MICRO_KERNEL_CONFIG_H


#ifdef __cplusplus
extern "C" {
#endif


//******************************* Includes **********************************//

/* 0.come from C standard library headers */
#include <stdint.h>


//******************************* Includes **********************************//


//******************************** Defines  *********************************//

/**
 * @brief  Scheduler mode selection constants.
 * - MK_MODE_QUEUE:  Uses a lock-free FIFO queue for tracking full data events.
 * - MK_MODE_BITMAP: Uses a single bitmask for ultra-fast light event signaling.
 */
#define MK_MODE_QUEUE  0
#define MK_MODE_BITMAP 1

/**
 * @brief  Default scheduler mode configuration switch.
 * Can be overridden globally via compiler symbols or system configuration headers.
 */
#ifndef MK_CFG_SCHEDULER_MODE
#define MK_CFG_SCHEDULER_MODE  MK_MODE_QUEUE
#endif

/**
 * @brief  Broadcast Target Identifier.
 * When the event's target_id matches this value, the internal scheduler will
 * route the message packet to all active objects statically registered.
 */
#define MK_BROADCAST_ID   0xFF 

/**
 * @brief  Dummy Event type identifier for Bitmap Mode.
 * Used internally to maintain consistent pf_dispatch function signatures.
 */
#if (MK_CFG_SCHEDULER_MODE == MK_MODE_BITMAP)
#define MK_EVENT    0xFE 
#endif /* (MK_CFG_SCHEDULER_MODE == MK_MODE_BITMAP) */

/**
 * @brief  Reserved System Signals (0x00 to 0x0F)
 * Used strictly for micro-kernel lifecycle and fundamental runtime ticks.
 */
#define SIG_INIT          0x00  /* System Boot/Power-On Reset signal */
#define SIG_TICK          0x01  /* Global timer tick or periodic tick signal */

/**
 * @brief  User Defined Application Signals (0x10 and above)
 * App developers must specify custom signaling protocols here.
 */
#define SIG_LED           0x10  /* Signal identifier for LED module operations */

/**
 * @brief  Per-Active-Object event queue capacity.
 * Each AO has its own lock-free SPSC ring buffer of this depth.
 * MUST be a power of 2 (compile-time assert below ensures this).
 */
#ifndef MK_AO_QUEUE_CAPACITY
#define MK_AO_QUEUE_CAPACITY  8
#endif
#if (MK_AO_QUEUE_CAPACITY & (MK_AO_QUEUE_CAPACITY - 1)) != 0
#error "MK_AO_QUEUE_CAPACITY must be a power of 2!"
#endif

/**
 * @brief  Maximum number of Active Objects (AOs) supported by the scheduler.
 * This limit is set to prevent excessive resource usage and ensure efficient
 * memory footprint. Moved from core.h in v2.0 to centralise all tunables here.
 */
#ifndef AO_MAX
#define AO_MAX  16
#endif

/**
 * @brief  Scheduler tick interval in microseconds.
 * This defines how often the scheduler runs and checks for events.
 */
#ifndef MK_SCHEDULER_TICK_INTERVAL_US
#define MK_SCHEDULER_TICK_INTERVAL_US  1000  /* 1 ms tick interval */
#endif

/**
 * @brief  Maximum number of software timers supported.
 */
#ifndef MK_TIMER_MAX
#define MK_TIMER_MAX  16
#endif

/**
 * @brief  Hardware Abstraction: WFI (Wait For Interrupt) — enter sleep mode.
 * Defined here so core.c and queue.c don't need CMSIS headers.
 */
#if defined(__GNUC__) || defined(__clang__)
    #define MK_WFI()              __asm volatile("wfi" ::: "memory")
    #define MK_DISABLE_IRQ()      __asm volatile("cpsid i" ::: "memory")
    #define MK_ENABLE_IRQ()       __asm volatile("cpsie i" ::: "memory")
    #define MK_GET_PRIMASK()      ({ uint32_t __mk_primask; __asm volatile("mrs %0, primask" : "=r"(__mk_primask)); __mk_primask; })
    #define MK_DATA_MB()          __asm volatile("dmb" ::: "memory")
#elif defined(__CC_ARM)
    /* Keil MDK ARMCC Build Environment */
    #define MK_WFI()              __wfi()
    #define MK_DISABLE_IRQ()      __disable_irq()
    #define MK_ENABLE_IRQ()       __enable_irq()
    #define MK_GET_PRIMASK()      0   /* ARMCC: assume interrupts enabled, caller must manage */
    #define MK_DATA_MB()          __DMB()
#else
    /* Fallback — no sleep, no protection (single-threaded host testing) */
    #define MK_WFI()              ((void)0)
    #define MK_DISABLE_IRQ()      ((void)0)
    #define MK_ENABLE_IRQ()       ((void)0)
    #define MK_GET_PRIMASK()      0
    #define MK_DATA_MB()          ((void)0)
#endif

/**
 * @brief  Hardware Accelerated Priority Extraction Macro (O(1)).
 * @details Takes advantage of core-level Count Leading Zeros (CLZ) instructions
 * to evaluate the highest priority task in a single CPU machine cycle.
 *
 * Example Usage:
 * uint32_t ready_map = 0x00000420; // Bits 5 and 10 are active
 * uint8_t highest = MK_GET_HIGHEST_PRIO(ready_map); // Returns 10
 *
 * @param  map The 32-bit bitmask to look up for active task flags.
 */
#if defined(__GNUC__) || defined(__clang__)
    /* GCC/Clang Build Environment */
    #define MK_GET_HIGHEST_PRIO(map) (31U - __builtin_clz(map))
#elif defined(__CC_ARM)
    /* Keil MDK ARMCC Build Environment */
    #define MK_GET_HIGHEST_PRIO(map) (31U - __clz(map))
#else
    /* Fallback universal software wrapper for standalone platforms */
    static inline uint8_t mk_soft_clz(uint32_t map) {
        uint8_t pos = 0;
        while (map >>= 1) pos++;
        return pos;
    }
    #define MK_GET_HIGHEST_PRIO(map) mk_soft_clz(map)
#endif


//******************************** Defines  *********************************//


#ifdef __cplusplus
}
#endif


#endif /* MID_MICRO_KERNEL_CONFIG_H */


/************************ (C) COPYRIGHT 2026 suyan *********END OF FILE*******/
