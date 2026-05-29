/******************************************************************************
 * @file    mid_micro_kernel_queue.h
 * @brief   Per-Active-Object Lock-free SPSC Event Queue API.
 *
 * Detailed Description:
 * - Architecture Role: This header defines the interfaces for the per-AO
 * lock-free Single-Producer Single-Consumer (SPSC) ring buffer. Each Active
 * Object owns its own queue instance; ISRs enqueue events by targeting a
 * specific AO's queue handle.
 *
 * - Multi-Producer Safety: The enqueue operation wraps its write sequence in
 * a minimal PRIMASK critical section (Cortex-M), allowing concurrent calls
 * from multiple ISR contexts without data corruption.
 *
 * - Performance: Power-of-two capacity enables bitmask wrapping instead of
 * expensive modulo operations on basic MCUs without hardware division.
 *
 * @note When to prefer v1.x over v2.0:
 *   v1.x uses a single statically-allocated global queue (no per-AO overhead).
 *   It is simpler and slightly faster (no parameterised q pointer, no critical
 *   section). If you have exactly ONE producer ISR and don't need event
 *   isolation between AOs, v1.x's global mk_queue_enqueue(msg) is sufficient.
 *
 * Version History:
 * - 2.0.0 (2026-05-28):
 *   - API changed to mk_queue_t pointer-based (per-AO instance).
 *   - Enqueue wrapped in PRIMASK critical section for multi-producer safety.
 *   - Added DMB data barrier between buffer write and index update.
 *   - Added mk_queue_init() with external buffer + capacity parameter.
 *   - Added mk_queue_is_empty() convenience function.
 * - 1.0.0 (2026-02-04): Initial release — global static SPSC ring buffer.
 *
 * Author:   suyan
 * Date:     2026-05-28
 * Version:  2.0.0
 *****************************************************************************/

#ifndef MID_MICRO_KERNEL_QUEUE_H
#define MID_MICRO_KERNEL_QUEUE_H


#ifdef __cplusplus
extern "C" {
#endif


//******************************* Includes **********************************//

/* 0.come from C standard library headers */
#include <stdint.h>
#include <stdbool.h>

/* 1.come from mid layer file        */
#include "mid_micro_kernel_type.h"


//******************************* Includes **********************************//


//*********************** Types or enum or struct ***************************//

/**
 * @brief  Status return codes for event queue operations.
 */
typedef enum
{
    MK_QUEUE_OK     = 0,  /**< Operation completed successfully. */
    MK_QUEUE_FULL   = 1,  /**< Queue is full, enqueue operation failed. */
    MK_QUEUE_EMPTY  = 2,  /**< Queue is empty, dequeue operation failed. */
    MK_QUEUE_ERR    = 3   /**< Internal error or unexpected state detected. */
} mk_queue_status_t;


//*********************** Types or enum or struct ***************************//


//****************************** Declaring **********************************//

/**
 * @brief  Initialize a per-AO event queue.
 * @details Sets read/write indices to zero, links the buffer and computes
 * the bitmask from the capacity (assumed power of 2).
 * @param  q        Pointer to the queue handle (embedded in mk_active_object_t).
 * @param  buffer   Pointer to the ring buffer storage array.
 * @param  capacity Number of elements in the buffer (MUST be power of 2).
 */
void mk_queue_init(mk_queue_t *q, mk_event_msg_t *buffer, uint32_t capacity);

/**
 * @brief  Enqueue an event message (ISR Safe, Multi-Producer).
 * @details Copies the event into the ring buffer under a minimal PRIMASK
 * critical section. Safe to call from multiple ISR contexts concurrently.
 * @param  q   Pointer to the target AO's queue handle.
 * @param  msg Pointer to the event message to enqueue.
 * @return mk_queue_status_t
 * - MK_QUEUE_OK:   Event enqueued successfully.
 * - MK_QUEUE_FULL: Queue is full, event discarded.
 */
mk_queue_status_t mk_queue_enqueue(mk_queue_t *q, const mk_event_msg_t *msg);

/**
 * @brief  Dequeue an event message (Scheduler only, single consumer).
 * @details Reads the oldest event from the ring buffer. This function is
 * called exclusively by the scheduler (single consumer), so no critical
 * section is needed for the read-side.
 * @param  q   Pointer to the AO's queue handle.
 * @param  msg Pointer to the destination struct for the dequeued event.
 * @return mk_queue_status_t
 * - MK_QUEUE_OK:   Event dequeued successfully.
 * - MK_QUEUE_EMPTY: Queue is empty.
 */
mk_queue_status_t mk_queue_dequeue(mk_queue_t *q, mk_event_msg_t *msg);

/**
 * @brief  Check if a queue is empty.
 * @param  q Pointer to the queue handle.
 * @return true if the queue contains no events, false otherwise.
 */
bool mk_queue_is_empty(mk_queue_t *q);


//****************************** Declaring **********************************//


#ifdef __cplusplus
}
#endif


#endif /* MID_MICRO_KERNEL_QUEUE_H */


/************************ (C) COPYRIGHT 2026 suyan *********END OF FILE*******/