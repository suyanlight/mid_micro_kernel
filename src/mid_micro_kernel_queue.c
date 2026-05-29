/******************************************************************************
 * @file    mid_micro_kernel_queue.c
 * @brief   Per-Active-Object Lock-free SPSC Event Queue Implementation.
 *
 * Detailed Description:
 * - Architecture Role: This module implements the per-AO lock-free SPSC ring
 * buffer. Each Active Object owns its own queue instance, providing event
 * isolation between application modules.
 *
 * - Multi-Producer Safety: The enqueue path uses a PRIMASK critical section
 * (Cortex-M) to protect the write index + data copy sequence, making it safe
 * for concurrent calls from Timer ISR, UART ISR, and other interrupt sources.
 *
 * - Performance: Power-of-two capacity enables bitmask wrapping instead of
 * modulo. The dequeue path (single consumer, scheduler only) needs no
 * critical section at all.
 *
 * @note When to prefer v1.x over v2.0:
 *   v1.x enqueue is ~50% fewer instructions (no PRIMASK save/restore, no DMB).
 *   On Cortex-M0 where PRIMASK read-back is not available and DMB is a NOP
 *   (architecturally treated as DSB), the protection adds unnecessary cycles.
 *   If your system has a single producer ISR, use v1.x for maximum throughput.
 *
 * Version History:
 * - 2.0.0 (2026-05-28):
 *   - Per-AO queue instance via mk_queue_t pointer.
 *   - Multi-producer safety: PRIMASK critical section + DMB in enqueue.
 *   - Dequeue unchanged (single consumer, no protection needed).
 * - 1.0.0 (2026-02-04): Initial release — global SPSC ring buffer.
 *
 * Author:   suyan
 * Date:     2026-05-28
 * Version:  2.0.0
 *****************************************************************************/


//******************************* Includes **********************************//

/* 0.come from C standard library headers */
#include <string.h> /* For memset */

/* 1.come from mid layer file        */
#include "mid_micro_kernel_queue.h"
#include "mid_micro_kernel_type.h"
#include "mid_micro_kernel_config.h"


//******************************* Includes **********************************//


//******************************* Implementation ****************************//

/**
 * @brief  Initialize a per-AO event queue.
 *
 * @param  q        Pointer to the queue handle.
 * @param  buffer   Pointer to the ring buffer storage.
 * @param  capacity Number of elements (MUST be power of 2).
 */
void mk_queue_init(mk_queue_t *q, mk_event_msg_t *buffer, uint32_t capacity)
{
    q->buffer = buffer;
    q->rd_idx = 0;
    q->wr_idx = 0;
    q->mask   = capacity - 1;
    
    (void)memset((void*)buffer, 0, capacity * sizeof(mk_event_msg_t));
}

/**
 * @brief  Enqueue an event message (ISR Safe, Multi-Producer).
 *
 * Wraps the write sequence in a PRIMASK critical section so that concurrent
 * calls from nested ISRs (e.g. Timer + UART) do not corrupt the queue state.
 *
 * @param  q   Pointer to the target AO's queue handle.
 * @param  msg Pointer to the event message to enqueue.
 * @return mk_queue_status_t
 */
mk_queue_status_t mk_queue_enqueue(mk_queue_t *q, const mk_event_msg_t *msg)
{
    /* ── Enter critical section (disable IRQs, ~3 cycles on Cortex-M) ── */
    MK_DISABLE_IRQ();
    
    /* 1. Calculate next write index using bitmask wrapping */
    uint32_t next_write_idx = (q->wr_idx + 1) & q->mask;
    
    /* 2. Check for queue full */
    if (next_write_idx == q->rd_idx)
    {
        MK_ENABLE_IRQ();
        return MK_QUEUE_FULL;
    }
    
    /* 3. Copy event data into the buffer */
    q->buffer[q->wr_idx] = *msg;
    
    /* 4. Data memory barrier: ensure the write completes before
     *    the index update is visible to the consumer. */
    MK_DATA_MB();
    
    /* 5. Advance write index (atomic store in critical section) */
    q->wr_idx = next_write_idx;
    
    /* ── Exit critical section ── */
    MK_ENABLE_IRQ();
    
    return MK_QUEUE_OK;
}

/**
 * @brief  Dequeue an event message (Scheduler only, single consumer).
 *
 * Called exclusively by the scheduler main loop. No critical section needed
 * because the scheduler is the sole consumer and runs at thread (background)
 * priority. Reads the write index once to detect empty vs. non-empty.
 *
 * @param  q   Pointer to the AO's queue handle.
 * @param  msg Pointer to store the dequeued event.
 * @return mk_queue_status_t
 */
mk_queue_status_t mk_queue_dequeue(mk_queue_t *q, mk_event_msg_t *msg)
{
    /* 1. Check for queue empty (read volatile wr_idx once) */
    if (q->rd_idx == q->wr_idx)
    {
        return MK_QUEUE_EMPTY;
    }
    
    /* 2. Copy event data from the buffer */
    *msg = q->buffer[q->rd_idx];
    
    /* 3. Advance read index */
    q->rd_idx = (q->rd_idx + 1) & q->mask;
    
    return MK_QUEUE_OK;
}

/**
 * @brief  Check if a queue is empty.
 *
 * @param  q Pointer to the queue handle.
 * @return true if empty, false otherwise.
 */
bool mk_queue_is_empty(mk_queue_t *q)
{
    return (q->rd_idx == q->wr_idx);
}


//******************************* Implementation ****************************//


/************************ (C) COPYRIGHT 2026 suyan *********END OF FILE*******/