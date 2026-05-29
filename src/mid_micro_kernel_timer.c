/******************************************************************************
 * @file    mid_micro_kernel_timer.c
 * @brief   Software Timer Service Implementation.
 *
 * Detailed Description:
 * - Architecture Role: Provides lightweight software timers that fire events
 * to Active Objects on expiry. Designed for tick-driven bare-metal systems
 * with a 1 ms SysTick base.
 *
 * - Memory: Static array of mk_timer_t control blocks; no dynamic allocation.
 *
 * - Concurrency: mk_timer_start() / mk_timer_stop() are safe to call from
 * ISR context (they only write one struct). mk_timer_tick() is designed to
 * run in the SysTick ISR; it fires post callbacks inline.
 *
 * @note When to prefer NOT using this module (v1.x micro-kernel):
 *   v1.x has no software timer service — all timing is handled by the
 *   application through raw tick-counter comparisons. If you only have 1-2
 *   periodic tasks, inline tick checks in MK_PT_WAIT_UNTIL may be simpler.
 *
 * @since 2.0.0 — introduced as part of the micro-kernel v2.0 release.
 *
 * Author:   suyan
 * Date:     2026-05-28
 * Version:  2.0.0
 *****************************************************************************/


//******************************* Includes **********************************//

/* 0.come from C standard library headers */
#include <string.h>

/* 1.come from mid layer file        */
#include "mid_micro_kernel_timer.h"


//******************************* Includes **********************************//


//************************** Init Internal Value ****************************//

/** Static pool of software timer control blocks */
static mk_timer_t s_timers[MK_TIMER_MAX];

/** Callback for posting expiry events to the scheduler */
static mk_timer_post_cb_t s_post_cb = 0;


//************************** Init Internal Value ****************************//


//******************************* Implementation ****************************//

/**
 * @brief  Initialize the software timer service.
 *
 * Marks all timer slots as inactive. Must be called once before any
 * mk_timer_start() or mk_timer_tick().
 */
void mk_timer_init(void)
{
    (void)memset(s_timers, 0, sizeof(s_timers));
}

/**
 * @brief  Register the callback for posting expiry events.
 *
 * @param  cb Function pointer (typically mk_post_event).
 */
void mk_timer_set_post_cb(mk_timer_post_cb_t cb)
{
    s_post_cb = cb;
}

/**
 * @brief  Start or restart a software timer.
 *
 * If the timer slot is already active, it is silently restarted with the
 * new parameters. Call mk_timer_stop() first if you want a stop-then-start
 * semantic.
 *
 * @param  timer_id  Index [0 .. MK_TIMER_MAX-1].
 * @param  target_id AO to receive the expiry event.
 * @param  signal    Event signal.
 * @param  period_ms Duration in milliseconds.
 * @param  repeat    true = periodic, false = one-shot.
 */
void mk_timer_start(uint8_t timer_id, uint8_t target_id, uint8_t signal,
                    uint32_t period_ms, bool repeat)
{
    if (timer_id >= MK_TIMER_MAX)
    {
        return;
    }

    s_timers[timer_id].target_id    = target_id;
    s_timers[timer_id].signal       = signal;
    s_timers[timer_id].period_ms    = period_ms;
    s_timers[timer_id].remaining_ms = period_ms;
    s_timers[timer_id].repeat       = repeat;
    s_timers[timer_id].active       = true;
}

/**
 * @brief  Stop a running timer.
 *
 * @param  timer_id Index of the timer to stop.
 */
void mk_timer_stop(uint8_t timer_id)
{
    if (timer_id >= MK_TIMER_MAX)
    {
        return;
    }

    s_timers[timer_id].active = false;
}

/**
 * @brief  Check whether a timer is currently active.
 *
 * @param  timer_id Index of the timer to query.
 * @return true if running, false otherwise.
 */
bool mk_timer_is_active(uint8_t timer_id)
{
    if (timer_id >= MK_TIMER_MAX)
    {
        return false;
    }

    return s_timers[timer_id].active;
}

/**
 * @brief  Timer tick handler — call from SysTick ISR (every 1 ms).
 *
 * Decrements all active timers. Expired timers fire their event through the
 * registered post callback. One-shot timers auto-deactivate; periodic timers
 * reload their period and continue.
 *
 * @note  The callback (mk_post_event) uses a PRIMASK critical section
 * internally, so it is safe to call directly from this ISR context.
 */
void mk_timer_tick(void)
{
    for (uint8_t i = 0; i < MK_TIMER_MAX; i++)
    {
        if (!s_timers[i].active)
        {
            continue;
        }

        /* Decrement remaining time */
        if (--s_timers[i].remaining_ms > 0)
        {
            continue;  /* Not yet expired */
        }

        /* ── Timer expired ── */
        if (s_post_cb != 0)
        {
            s_post_cb(s_timers[i].target_id, s_timers[i].signal, 0);
        }

        if (s_timers[i].repeat)
        {
            /* Periodic: reload */
            s_timers[i].remaining_ms = s_timers[i].period_ms;
        }
        else
        {
            /* One-shot: deactivate */
            s_timers[i].active = false;
        }
    }
}


//******************************* Implementation ****************************//


/************************ (C) COPYRIGHT 2026 suyan *********END OF FILE*******/