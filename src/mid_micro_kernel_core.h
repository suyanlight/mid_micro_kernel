/******************************************************************************
 * @file    mid_micro_kernel_core.h
 * @brief   Micro-Kernel Scheduler Core API.
 *
 * Detailed Description:
 * - Architecture Role: This header exposes the core application programming
 * interfaces (APIs) for the Micro-Kernel Scheduler. It provides mechanisms
 * to initialize the kernel, register Active Objects (AOs), and run the
 * main event loop.
 *
 * - v2.0 Upgrade: Per-AO event queues with round-robin (Queue mode) or CLZ
 * priority (Bitmap mode). New mk_post_event/mk_post_broadcast/mk_get_tick
 * APIs. Low-power WFI idle. Multi-producer ISR-safe event posting.
 *
 * @note When to prefer v1.x over v2.0:
 *   If your system uses a SINGLE global FIFO event queue (e.g. all events
 *   flow through one ISR into one pipe) and strict temporal ordering matters,
 *   v1.x mk_queue_dequeue semantics preserve the exact FIFO arrival sequence.
 *   v2.0 round-robin per-AO dispatch changes event ordering: an event posted
 *   to AO#0 may execute after one posted to AO#7, even if AO#0's arrived first.
 *
 * Version History:
 * - 2.0.0 (2026-05-28):
 *   - Per-AO event queues; round-robin (Queue) or CLZ priority (Bitmap).
 *   - Added mk_post_event(), mk_post_broadcast(), mk_get_tick() APIs.
 *   - Enabled WFI low-power idle in both modes.
 * - 1.2.0 (2026-05-28): Telemetry / fault callbacks + mk_set_tick_callback().
 * - 1.0.0 (2026-02-04): Initial release.
 *
 * Author:   suyan
 * Date:     2026-05-28
 * Version:  2.0.0
 *****************************************************************************/
#ifndef MID_MICRO_KERNEL_CORE_H
#define MID_MICRO_KERNEL_CORE_H


#ifdef __cplusplus
extern "C" {
#endif


//******************************* Includes **********************************//

#include "mid_micro_kernel_type.h"


//******************************* Includes **********************************//


//******************************** Defines  *********************************//

/* AO_MAX, MK_SCHEDULER_TICK_INTERVAL_US moved to mid_micro_kernel_config.h.
 * MK_VALUE_INIT removed (redundant with PT_INITVALUE in mid_micro_kernel_pt.h). */

//******************************** Defines  *********************************//


//*********************** Types or enum or struct ***************************//

/**
 * @brief  Fault callback function pointer type.
 * @param  bad_ao_id The ID of the Active Object that exceeded the timeout threshold.
 * @param  cost_time The actual execution time (in ms) that caused the fault.
 */
typedef void (*mk_fault_cb_t)(uint8_t bad_ao_id, uint32_t cost_time);

/**
 * @brief  System tick callback function pointer type.
 * @return uint32_t Current system timestamp (typically in milliseconds).
 */
typedef uint32_t (*mk_get_tick_cb_t)(void);


//*********************** Types or enum or struct ***************************//


//****************************** Declaring **********************************//

/**
 * @brief  Initialize the Micro-Kernel Scheduler.
 * @details
 * This function initializes the internal data structures and state of the
 * Micro-Kernel Scheduler. It clears the AO table and prepares the event queue
 * or bitmap based on the configuration.
 */
void mk_scheduler_init(void);

/**
 * @brief  Run the Micro-Kernel Scheduler.
 * @details
 * This function executes the main scheduling logic of the Micro-Kernel. 
 * It broadcasts the initialization signal and enters an infinite Run-to-Completion 
 * event loop.
 */
void mk_scheduler_run(void);

/**
 * @brief  Register fault callback for telemetry.
 *
 * @param  cb Function pointer to the fault handler.
 */
void mk_set_fault_callback(mk_fault_cb_t cb);

/**
 * @brief  Set the timeout threshold for AO execution.
 *
 * @param  threshold_ms Maximum allowed execution time per dispatch in milliseconds.
 */
void mk_set_timeout_threshold(uint32_t threshold_ms);

/**
 * @brief  Register an Active Object with the kernel.
 *
 * @param  ao Pointer to the Active Object instance.
 */
void mk_register_ao(mk_active_object_t* ao);

#if (MK_CFG_SCHEDULER_MODE == MK_MODE_BITMAP)
/**
 * @brief  Set an Active Object to the ready state (Bitmap Mode only).
 * @note   Compile-time guarded: in Queue Mode this function is not declared.
 *         Calling it when compiled for Queue Mode produces a linker error.
 * @param  ao_id The ID of the Active Object to mark as ready.
 */
void mk_set_task_ready(uint8_t ao_id);
#endif /* MK_MODE_BITMAP */

/**
 * @brief  Register the system tick provider.
 *
 * @param  cb Function pointer to get the current system time in milliseconds.
 */
void mk_set_tick_callback(mk_get_tick_cb_t cb);

/**
 * @brief  Post an event to a specific Active Object's queue (ISR Safe).
 * @details Enqueues the event to the target AO's private lock-free queue.
 * Safe to call from any ISR context. In Bitmap mode, also sets the AO's
 * ready bit to wake the scheduler.
 *
 * @param  target_id Target AO ID (0 ~ AO_MAX-1).
 * @param  signal    Event signal identifier.
 * @param  params    Optional data pointer (zero-copy).
 */
void mk_post_event(uint8_t target_id, uint8_t signal, void *params);

/**
 * @brief  Post an event to ALL registered Active Objects (ISR Safe).
 * @details Enqueues the same event (with per-AO target_id) to every active
 * AO's queue. Used for system-wide broadcasts such as SIG_INIT or power
 * management notifications.
 *
 * @param  signal Event signal identifier.
 * @param  params Optional data pointer (shared, AO must not free it).
 */
void mk_post_broadcast(uint8_t signal, void *params);

/**
 * @brief  System tick query — returns the current ms tick count.
 * @details Backs the MK_PT_DELAY macro. Reads from the tick callback
 * registered via mk_set_tick_callback().
 * @return uint32_t Current tick count in milliseconds.
 */
uint32_t mk_get_tick(void);


//****************************** Declaring **********************************//


#ifdef __cplusplus
}
#endif


#endif /* MID_MICRO_KERNEL_CORE_H */


/************************ (C) COPYRIGHT 2026 suyan *********END OF FILE*******/
