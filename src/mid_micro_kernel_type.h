/******************************************************************************
 * @file    mid_micro_kernel_type.h
 * @brief   Data types and Active Object definitions for mid Micro-Kernel.
 *
 * Detailed Description:
 * - Architecture Role: This header defines the foundational data structures 
 * required for the event-driven micro-kernel architecture. It standardizes 
 * the structures for inter-task event messages and polymorphic active objects.
 *
 * - Key Features:
 * - Standardized event format (mk_event_msg_t) supporting zero-copy data 
 * payloads via universal generic void pointers (params).
 * - Active Object (mk_active_object_t) template leveraging structure nesting 
 * to support OOP-C style polymorphism and private state isolation.
 *
 * @note When to prefer v1.x over v2.0:
 *   v1.x mk_active_object_t is slimmer (no embedded queue, no delay_deadline).
 *   If your AO structs are already ROM-constrained or you use an alternative
 *   event-passing mechanism (e.g. shared globals + set-ready-bit), v1.x is
 *   lighter. Per-AO queues add MK_AO_QUEUE_CAPACITY * sizeof(event) bytes per AO.
 *
 * Version History:
 * - 2.0.0 (2026-05-28):
 *   - Added mk_queue_t (per-AO lock-free SPSC queue handle).
 *   - Added queue / queue_storage[] / delay_deadline fields to mk_active_object_t.
 *   - Includes mid_micro_kernel_config.h for MK_AO_QUEUE_CAPACITY.
 * - 1.0.1 (2026-05-28):
 *   - Synchronized pf_dispatch return type to 'int' for protothreads.
 *   - Cleaned up obsolete parameter descriptions (p_data, p_private).
 * - 1.0.0 (2026-02-04): Initial formal release.
 *
 * Author:   suyan
 * Date:     2026-05-28
 * Version:  2.0.0
 *****************************************************************************/

#ifndef MID_MICRO_KERNEL_TYPE_H
#define MID_MICRO_KERNEL_TYPE_H


#ifdef __cplusplus
extern "C" {
#endif


//******************************* Includes **********************************//

/* 1.come from mid layer file        */
#include "mid_micro_kernel_pt.h"
#include "mid_micro_kernel_config.h"


//******************************* Includes **********************************//


//*********************** Types or enum or struct ***************************//

/**
 * @brief  Structure representing an event message packet.
 * @details Used for routing asynchronous signals and generic data payloads 
 * between different Active Objects or from ISRs to tasks.
 */
typedef struct mk_event_msg
{
    uint8_t      target_id;     /**< Target AO ID for message routing. */
    uint8_t   event_signal;     /**< Signal/event type identifier. */
    void           *params;     /**< Pointer to event-specific data (optional payload). */
} mk_event_msg_t;

/**
 * @brief  Lock-free SPSC queue handle (per-AO).
 * @details Each Active Object holds one of these as its private event inbox.
 * The buffer pointer, read/write indices, and bitmask wrap provide O(1)
 * enqueue/dequeue without modulo division or critical sections on single-producer.
 */
typedef struct mk_queue
{
    mk_event_msg_t     *buffer; /**< Pointer to ring buffer storage.       */
    volatile uint32_t   rd_idx; /**< Consumer read index (updated by scheduler). */
    volatile uint32_t   wr_idx; /**< Producer write index (updated by ISRs).    */
    uint32_t            mask;   /**< Capacity-1, for bitmask wrapping.          */
} mk_queue_t;

/**
 * @brief  Structure representing an Active Object (AO) base class.
 * @details Serves as the super class in OOP-C. Business modules must embed 
 * this structure as their first member to achieve inheritance and state tracking.
 * Each AO now carries its own lock-free event queue and a delay deadline field
 * for protothread-based blocking delays.
 */
typedef struct mk_active_object
{
    uint8_t      active_object_id; /**< Unique AO ID used for O(1) routing table. */   
    mk_pt_t       pt_state;         /**< Embedded protothread context for execution state. */
    
    /** * @brief AO's polymorphic event handler function pointer.
     * @param ao    Pointer to the active object instance (equivalent to 'this' pointer).
     * @param event Pointer to the incoming event message packet.
     * @return int  Protothread state exit code (e.g., PT_WAITING, PT_YIELDED).
     */
    int (*pf_dispatch)(struct mk_active_object *ao, mk_event_msg_t *event);
    
    mk_queue_t        queue;              /**< Per-AO lock-free event queue.      */
    mk_event_msg_t    queue_storage[MK_AO_QUEUE_CAPACITY]; /**< Inline queue buffer. */
    uint32_t          delay_deadline;     /**< Absolute tick deadline for MK_PT_DELAY. */
} mk_active_object_t;


//*********************** Types or enum or struct ***************************//


#ifdef __cplusplus
}
#endif


#endif /* MID_MICRO_KERNEL_TYPE_H */


/************************ (C) COPYRIGHT 2026 suyan *********END OF FILE*******/
