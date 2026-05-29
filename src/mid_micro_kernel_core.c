/******************************************************************************
 * @file    mid_micro_kernel_core.c
 * @brief   Micro-Kernel Scheduler Core Implementation.
 *
 * Detailed Description:
 * - Architecture Role: This module is the core of the event-driven micro-kernel.
 * It manages the lifecycle and execution of Active Objects (AOs).
 *
 * - Dual Mode Support (both modes now use per-AO queues):
 * - Queue Mode (MK_MODE_QUEUE): Round-robin across per-AO event queues.
 *   Each AO has its own SPSC ring buffer; the scheduler polls them in
 *   priority-ID order. No single AO can flood or starve another.
 * - Bitmap Mode (MK_MODE_BITMAP): CLZ-based O(1) priority scheduling.
 *   The scheduler picks the highest-priority ready AO, dispatches its event,
 *   then re-evaluates the bitmask.
 *
 * - Multi-Producer Safety: mk_post_event() wraps the enqueue in a PRIMASK
 *   critical section, safe for concurrent calls from any ISR context.
 *
 * - Telemetry & Safety: Integrates execution time monitoring and fault
 *   callbacks to detect individual AOs blocking the system.
 *
 * - Low-Power Idle: When no events are pending, the CPU enters WFI sleep.
 *   Any interrupt (SysTick, UART, GPIO) wakes it for the next dispatch cycle.
 *
 * @note When to prefer v1.x over v2.0:
 *   v1.x dequeues from one global queue and dispatches by target_id embedded
 *   in the event — strict FIFO order is guaranteed. v2.0 round-robin scans
 *   per-AO queues in ID order, so an event enqueued to AO#7 may be processed
 *   before an older event in AO#0's queue. If temporal ordering across AOs
 *   matters (e.g. a sensor read MUST finish before its result is displayed),
 *   v1.x's global queue ensures that ordering by construction.
 *   Additionally, v1.x has no mk_post_event/mk_get_tick/mk_broadcast_init
 *   helper functions — the code surface is smaller and easier to audit.
 *
 * Version History:
 * - 2.0.0 (2026-05-28):
 *   - Per-AO event queues with round-robin (Queue) / CLZ priority (Bitmap).
 *   - mk_post_event(), mk_post_broadcast(), mk_get_tick() implementations.
 *   - mk_dispatch_ao() helper extracted (shared telemetry logic).
 *   - WFI low-power idle enabled (was commented out in v1.x).
 *   - mk_register_ao() now initialises per-AO queue + delay_deadline.
 * - 1.0.0 (2026-02-04): Initial release with single global FIFO queue.
 *
 * Author:   suyan
 * Date:     2026-05-28
 * Version:  2.0.0
 *****************************************************************************/


//******************************* Includes **********************************//

/* 1.come from mid layer file        */
#include "mid_micro_kernel_core.h"
#include "mid_micro_kernel_queue.h"
#include "mid_micro_kernel_config.h"


//******************************* Includes **********************************//


//************************** Init Internal Value ****************************//

/* Bitmask of ready AOs (1 bit per AO), used primarily in BITMAP mode */
static volatile uint32_t s_ready_tasks_map = 0; 

/* Central table holding pointers to all registered Active Objects */
static mk_active_object_t* s_ao_table[AO_MAX] = {0};

/* Fault and Telemetry Variables */
static mk_fault_cb_t s_fault_cb = 0;       /* 错误回调函数指针 */
static uint32_t s_timeout_threshold = 5;   /* 默认单次任务超时阈值 5ms */
static mk_get_tick_cb_t s_get_tick_cb = 0; /* 系统时间获取回调函数指针 */


//************************** Init Internal Value ****************************//


//***************************** Local Helpers *******************************//

/**
 * @brief  Dispatch a single event to an AO with telemetry.
 *
 * @param  id     Target AO index.
 * @param  event  Pointer to the event message.
 */
static inline void mk_dispatch_ao(uint8_t id, mk_event_msg_t *event)
{
    /* [遥测打点开始] */
    uint32_t t_start = 0;
    if (s_get_tick_cb != 0)
    {
        t_start = s_get_tick_cb();
    }

    /* 🎯 核心执行 */
    s_ao_table[id]->pf_dispatch(s_ao_table[id], event);

    /* [遥测打点结束] 结算耗时 */
    uint32_t cost_time = 0;
    if (s_get_tick_cb != 0)
    {
        cost_time = s_get_tick_cb() - t_start;
    }

    /* 违约检查：如果耗时超过阈值，呼叫应用层审判 */
    if (cost_time > s_timeout_threshold && s_fault_cb != 0)
    {
        s_fault_cb(id, cost_time);
    }
}

/**
 * @brief  Broadcast SIG_INIT to all registered AOs at startup.
 */
static inline void mk_broadcast_init(void)
{
    mk_event_msg_t init_event;
    init_event.target_id    = MK_BROADCAST_ID;
    init_event.event_signal = SIG_INIT;
    init_event.params       = 0;

    for (uint8_t i = 0; i < AO_MAX; i++)
    {
        if (s_ao_table[i] != 0)
        {
            s_ao_table[i]->pf_dispatch(s_ao_table[i], &init_event);
        }
    }
}


//***************************** Local Helpers *******************************//


//******************************* Implementation ****************************//

/**
 * @brief  Register fault callback for telemetry.
 *
 * @param  cb Function pointer to the fault handler.
 */
void mk_set_fault_callback(mk_fault_cb_t cb) 
{
    s_fault_cb = cb;
}

/**
 * @brief  Register the system tick provider.
 *
 * @param  cb Function pointer to get the current system time in milliseconds.
 */
void mk_set_tick_callback(mk_get_tick_cb_t cb) 
{
    s_get_tick_cb = cb;
}

/**
 * @brief  Set the timeout threshold for AO execution.
 *
 * @param  threshold_ms Maximum allowed execution time per dispatch in milliseconds.
 */
void mk_set_timeout_threshold(uint32_t threshold_ms) 
{
    s_timeout_threshold = threshold_ms;
}

/**
 * @brief  Initialize the Micro-Kernel Scheduler.
 *
 * Clears the AO table, the ready bitmap, and all per-AO queues.
 */
void mk_scheduler_init(void) 
{
    /* 1. Clear AO table and ready map */
    for (uint8_t i = 0; i < AO_MAX; i++) 
    {
        s_ao_table[i] = 0;
    }
    s_ready_tasks_map = 0;
}

#if (MK_CFG_SCHEDULER_MODE == MK_MODE_BITMAP)
/**
 * @brief  Set an Active Object to the ready state (Bitmap Mode only).
 * @note   Compile-time guarded: this function only exists when scheduling
 *         mode is MK_MODE_BITMAP. In Queue Mode, calling it produces a
 *         linker error — that's by design, because the bitmap is never
 *         consulted in Queue Mode, so the call would be a silent no-op.
 *
 * @param  ao_id The ID of the Active Object to mark as ready.
 */
void mk_set_task_ready(uint8_t ao_id) 
{
    /* Set the bit corresponding to the AO ID to mark it as ready.
     * 必须关中断保护：s_ready_tasks_map 的 RMW 与调度器/其他 ISR 并发。 */
    MK_DISABLE_IRQ();
    s_ready_tasks_map |= (1U << ao_id);
    MK_ENABLE_IRQ();
}
#endif /* MK_MODE_BITMAP */

/**
 * @brief  Register an Active Object with the kernel.
 *
 * Also initialises the AO's private lock-free event queue and resets its
 * protothread coroutine state.
 *
 * @param  ao Pointer to the Active Object instance.
 */
void mk_register_ao(mk_active_object_t* ao) 
{
    if (ao != 0 && ao->active_object_id < AO_MAX) 
    {
        /* 1. 复位协程状态 */
        MK_PT_INIT(&ao->pt_state);
        
        /* 2. 初始化 per-AO 事件队列 */
        mk_queue_init(&ao->queue, ao->queue_storage, MK_AO_QUEUE_CAPACITY);
        
        /* 3. 清零 delay deadline */
        ao->delay_deadline = 0;
        
        /* 4. 存入大表 */
        s_ao_table[ao->active_object_id] = ao;
    }
}

/**
 * @brief  Post an event to a specific Active Object's queue (ISR Safe).
 *
 * Enqueues the event to the target AO's private lock-free queue under a
 * PRIMASK critical section. In Bitmap mode, also sets the AO's ready bit
 * so the scheduler knows to process it.
 *
 * @param  target_id Target AO ID (0 ~ AO_MAX-1).
 * @param  signal    Event signal identifier.
 * @param  params    Optional data pointer (zero-copy).
 */
void mk_post_event(uint8_t target_id, uint8_t signal, void *params)
{
    if (target_id >= AO_MAX || s_ao_table[target_id] == 0)
    {
        return;  /* 非法 ID 或 AO 未注册，静默丢弃 */
    }

    mk_event_msg_t evt;
    evt.target_id    = target_id;
    evt.event_signal = signal;
    evt.params       = params;

    (void)mk_queue_enqueue(&s_ao_table[target_id]->queue, &evt);

#if (MK_CFG_SCHEDULER_MODE == MK_MODE_BITMAP)
    /* Bitmap 模式下额外置位就绪位，通知调度器。
     * 必须关中断保护：后台线程可能在同时执行 &= ~(1<<N) 的 RMW 序列，
     * ISR 的 |= 可能与后台的旧值写回发生冲突，导致就绪位丢失（丢事件）。 */
    MK_DISABLE_IRQ();
    s_ready_tasks_map |= (1U << target_id);
    MK_ENABLE_IRQ();
#endif
}

/**
 * @brief  Post an event to ALL registered Active Objects (ISR Safe).
 *
 * @param  signal Event signal identifier.
 * @param  params Optional data pointer (shared; AO must not free it).
 */
void mk_post_broadcast(uint8_t signal, void *params)
{
    for (uint8_t i = 0; i < AO_MAX; i++)
    {
        if (s_ao_table[i] != 0)
        {
            mk_post_event(i, signal, params);
        }
    }
}

/**
 * @brief  System tick query — returns the current ms tick count.
 *
 * Backs the MK_PT_DELAY macro. If no tick callback is registered, returns 0.
 *
 * @return uint32_t Current tick count in milliseconds.
 */
uint32_t mk_get_tick(void)
{
    if (s_get_tick_cb != 0)
    {
        return s_get_tick_cb();
    }
    return 0;
}

/**
 * @brief  Run the Micro-Kernel Scheduler loop.
 *
 * Broadcasts SIG_INIT to all AOs, then enters the infinite Run-to-Completion
 * scheduling loop with the selected mode (Queue or Bitmap).
 *
 * Queue Mode  — Round-robin over per-AO event queues.
 * Bitmap Mode — CLZ O(1) priority scheduling with per-AO queues.
 *
 * When idle, the CPU enters WFI sleep; any interrupt wakes it.
 */
void mk_scheduler_run(void) 
{
    /* 1. 系统启动广播 */
    mk_broadcast_init();

    /* 2. 微内核超级大循环 */
    while (1) 
    {
#if (MK_CFG_SCHEDULER_MODE == MK_MODE_QUEUE)
        /* ------------------------------------------------------ */
        /* 模式 A：Per-AO 队列轮询调度 (Round-Robin)              */
        /* ------------------------------------------------------ */
        
        bool any_processed = false;
        
        for (uint8_t i = 0; i < AO_MAX; i++)
        {
            if (s_ao_table[i] == 0)
            {
                continue;
            }
            
            mk_event_msg_t current_event;
            if (mk_queue_dequeue(&s_ao_table[i]->queue, &current_event) == MK_QUEUE_OK)
            {
                any_processed = true;
                
                if (current_event.target_id == MK_BROADCAST_ID)
                {
                    /* 广播事件：遍历全表下发 */
                    for (uint8_t j = 0; j < AO_MAX; j++)
                    {
                        if (s_ao_table[j] != 0)
                        {
                            s_ao_table[j]->pf_dispatch(s_ao_table[j], &current_event);
                        }
                    }
                }
                else
                {
                    /* 单播事件：直接分发给队列所属的 AO */
                    mk_dispatch_ao(i, &current_event);
                }
            }
        }
        
        if (!any_processed)
        {
            /* 全系统无事件，进入低功耗睡眠 */
            MK_WFI();
        }

#elif (MK_CFG_SCHEDULER_MODE == MK_MODE_BITMAP)
        /* ------------------------------------------------------ */
        /* 模式 B：纯位图 O(1) 优先级调度                          */
        /* ------------------------------------------------------ */
        
        if (s_ready_tasks_map != 0) 
        {
            /* 1个时钟周期瞬间算出最高优先级 */
            uint8_t highest_prio = MK_GET_HIGHEST_PRIO(s_ready_tasks_map);
            
            /* 清除该位的就绪状态。
             * 必须关中断保护：ISR 可能在 mk_post_event 中并发执行 |=，
             * 与这里的 &= ~ 的 RMW 序列发生竞争，丢失 ISR 刚置位的 bit。 */
            MK_DISABLE_IRQ();
            s_ready_tasks_map &= ~(1U << highest_prio);
            MK_ENABLE_IRQ();
            
            if (highest_prio < AO_MAX && s_ao_table[highest_prio] != 0) 
            {
                mk_event_msg_t current_event;
                
                /* 优先从 AO 的队列中取数据事件 */
                if (mk_queue_dequeue(&s_ao_table[highest_prio]->queue, &current_event) != MK_QUEUE_OK)
                {
                    /* 队列为空：构造 Dummy 事件（纯位图触发）。
                     * 应用层应检查自己的私有标志位和缓存，不依赖 event data。
                     */
                    current_event.target_id    = highest_prio;
                    current_event.event_signal = MK_EVENT;
                    current_event.params       = 0;
                }
                
                /* 🎯 O(1) 核心执行 */
                mk_dispatch_ao(highest_prio, &current_event);
            }
        } 
        else 
        {
            /* 位图全零，进入低功耗模式 */
            MK_WFI();
        }
#endif
    }
}


//******************************* Implementation ****************************//


/************************ (C) COPYRIGHT 2026 suyan *********END OF FILE*******/