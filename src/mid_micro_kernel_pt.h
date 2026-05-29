/******************************************************************************
 * @file    mid_micro_kernel_pt.h
 * @brief   Zero-Stack Protothreads Engine for Bare-Metal Systems
 *
 * @details
 * This module implements a micro-kernel coroutine engine (Protothreads) designed 
 * exclusively for resource-constrained bare-metal environments. It leverages 
 * C switch-case state tracking with __LINE__ macros to provide true zero-stack 
 * overhead concurrency without requiring dedicated task stacks.
 *
 * Key characteristics:
 * - Zero RAM overhead: All coroutines share the system stack (no per-task stack allocation)
 * - Synchronous coding style for asynchronous execution (no nested state machines)
 * - Explicit yield/wait semantics via PT_WAIT_UNTIL() and PT_YIELD()
 * - Minimal CPU overhead (single uint16_t state per coroutine)
 *
 * @warning
 * This implementation assumes you have full control of stack management:
 * • Default usage expects a single system stack (typically MSP in Cortex-M)
 * • You MUST ensure sufficient stack space for deepest coroutine call chain
 * • PSP/MSP context switching is NOT handled here - integrate with your scheduler
 * • Local variables across yields require static/struct storage (volatile not sufficient)
 *
 * @usage
 * 1. Embed mk_pt_t in your Active Object (AO) struct
 * 2. Initialize with MK_PT_INIT()
 * 3. Wrap logic in MK_PT_BEGIN()/MK_PT_END()
 * 4. Use blocking-style waits: MK_PT_WAIT_UNTIL(pt, condition)
 *
 * Example:
 * typedef struct { mk_pt_t pt; } sensor_ao_t;
 * int read_sensor(sensor_ao_t *ao) {
 * MK_PT_BEGIN(&ao->pt);
 * MK_PT_WAIT_UNTIL(&ao->pt, adc_ready);  // Non-blocking wait
 * process_data();
 * MK_PT_END(&ao->pt);
 * }
 *
 * @note
 * • Designed for tick-driven schedulers (call AO functions periodically)
 * • Maximum 65535 line numbers per coroutine (practical limit: ~1000)
 * • Requires C99 or later (__LINE__ behavior critical)
 *
 * @note When to prefer v1.x over v2.0:
 *   v1.x protothread engine is functionally identical except for the MK_PT_DELAY
 *   macro. If you don't need blocking delays (all your waits are event-driven
 *   via MK_PT_WAIT_UNTIL), v1.x is sufficient and avoids the mk_get_tick()
 *   dependency that MK_PT_DELAY introduces.
 *
 * @version 2.0.0 — Changelog:
 *   v2.0.0: Added MK_PT_DELAY(ao, ms) blocking delay macro.
 *   v1.0.0: Initial release (MK_PT_BEGIN/END, MK_PT_WAIT_UNTIL, MK_PT_YIELD).
 *
 * @author  suyan
 * @date    2026-05-28
 *****************************************************************************/
#ifndef MID_MICRO_KERNEL_PT_H
#define MID_MICRO_KERNEL_PT_H


#ifdef __cplusplus
extern "C" {
#endif


//******************************* Includes **********************************//

/* 0.come from C standard library headers */
/* * If you are not using standard C library variable types, 
 * please replace the corresponding files.
 */
#include <stdint.h>


//******************************* Includes **********************************//


//******************************** Defines  *********************************//

/**
 * @brief Coroutine running state return code.
 */
#define PT_WAITING    0  /* Waiting for a certain condition.         */
#define PT_YIELDED    1  /* voluntarily yielded CPU.                 */
#define PT_EXITED     2  /* was forcibly exited.                     */
#define PT_ENDED      3  /* completed execution normally.            */

/**
 * @brief Initial state value for coroutine.
 */
#define PT_INITVALUE  0  /* Initial state value for coroutine.       */


//******************************** Defines  *********************************//


//*********************** Types or enum or struct ***************************//

/**
 * @brief Protothread context structure.
 * Stores the execution state (line number) of the coroutine.
 */
typedef struct mk_pt
{
    /* line number  */
    uint16_t line;
} mk_pt_t;


//*********************** Types or enum or struct ***************************//


//****************************** Control Macros *****************************//

/**
 * @brief  Initialize a protothread context.
 * @note   Must be called before the coroutine is executed for the first time.
 * @param  pt Pointer to the mk_pt_t structure.
 */
#define MK_PT_INIT(pt)                              \
            do                                      \
            {                                       \
                    (pt)->line = PT_INITVALUE;      \
            } while(0)

/**
 * @brief  Declaration of the start of a protothread.
 * @details This macro opens a hidden switch statement that evaluates the 
 * stored line number to jump directly to the last execution point.
 * @note   Must be paired with MK_PT_END() at the end of the function.
 * @param  pt Pointer to the mk_pt_t structure.
 */
#define MK_PT_BEGIN(pt)                             \
            switch((pt)->line)                      \
            {                                       \
                case 0:                             
                
/**
 * @brief  Block protothread until a specific condition is met (Non-blocking wait).
 * @details It stores the current __LINE__ into the context block. On the next entry, 
 * the switch-case will jump straight to the matching 'case __LINE__:'. 
 * If the condition remains false, it breaks out early and returns PT_WAITING.
 * @param  pt        Pointer to the mk_pt_t structure.
 * @param  condition The boolean expression to check.
 * @return PT_WAITING if condition is false.
 */
#define MK_PT_WAIT_UNTIL(pt, condition)             \
            do {                                    \
                    (pt)->line = __LINE__;          \
                    case __LINE__:                  \
                        if (!(condition))           \
                        {                           \
                            return PT_WAITING;      \
                        }                           \
            } while(0)                              

/**
 * @brief  Voluntarily yield the CPU to allow other tasks to run.
 * @details Saves the current line number and returns execution immediately. On the 
 * next execution cycle, the coroutine resumes exactly after this statement.
 * @param  pt Pointer to the mk_pt_t structure.
 * @return PT_YIELDED when invoked.
 */
#define MK_PT_YIELD(pt)                             \
            do {                                    \
                    (pt)->line = __LINE__;          \
                    case __LINE__:                  \
                        return PT_YIELDED;          \
            } while(0)                              

/**
 * @brief  Declaration of the end of a protothread.
 * @details Closes the hidden switch statement block opened by MK_PT_BEGIN(). 
 * It marks the context state as PT_ENDED permanently.
 * @param  pt Pointer to the mk_pt_t structure.
 * @return PT_ENDED when execution finishes normally.
 */
#define MK_PT_END(pt)                               \
            }                                       \
            (pt)->line = PT_ENDED;                  \
            return PT_ENDED

/**
 * @brief  Block protothread execution for a specified duration (milliseconds).
 * @details Saves the execution line and sets an absolute deadline using the AO's
 * delay_deadline field. On each resumption the macro compares the current tick
 * against the deadline and returns PT_WAITING until enough time has elapsed.
 *
 * @note  Requires the application to provide a function:
 *        uint32_t mk_get_tick(void);  — returns current ms tick count.
 *        This function is typically backed by the kernel's tick callback or a
 *        hardware SysTick counter.
 *
 * @param  ao Pointer to the mk_active_object_t instance.
 * @param  ms Delay duration in milliseconds.
 * @return PT_WAITING until the deadline elapses, then falls through.
 */
#define MK_PT_DELAY(ao, ms)                                                 \
            do {                                                            \
                extern uint32_t mk_get_tick(void);                          \
                (ao)->delay_deadline = mk_get_tick() + (uint32_t)(ms);      \
                (ao)->pt_state.line = __LINE__;                             \
                case __LINE__:                                              \
                    /* 防 uint32_t 溢出回绕：用有符号差代替直接比较。     */\
                    /* mk_get_tick() 约 49.7 天后回绕到 0，直接 < 比较    */\
                    /* 会把过期 deadline 误判为未到。减法差自动处理回绕。  */\
                    if ((int32_t)(mk_get_tick() - (ao)->delay_deadline) < 0)\
                    {                                                       \
                        return PT_WAITING;                                  \
                    }                                                       \
            } while(0)

/**
 * @brief  Advanced composite wait loop (PT Loop with background keep-alive).
 * @details Blocks execution until the condition is met. During the waiting period, 
 * if the condition is checked and found to be false, it executes the 
 * background_task statement once (e.g., feeding a Watchdog) before yielding.
 * @param  pt              Pointer to the mk_pt_t structure.
 * @param  condition       The boolean expression to check for breaking the loop.
 * @param  background_task A C statement/expression executed while waiting.
 * @return PT_WAITING if condition is false.
 */
#define MK_PT_WAIT_UNTIL_DO(pt, condition, background_task) \
            do {                                            \
                (pt)->line = __LINE__;                      \
                case __LINE__:                              \
                    if (!(condition))                       \
                    {                                       \
                        { background_task; }                \
                        return PT_WAITING;                  \
                    }                                       \
            } while(0)
  
            
//****************************** Control Macros *****************************//   


#ifdef __cplusplus
}
#endif


#endif /* MID_MICRO_KERNEL_PT_H */


/************************ (C) COPYRIGHT 2026 suyan *********END OF FILE*******/
