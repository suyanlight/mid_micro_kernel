/******************************************************************************
 * @file    mid_micro_kernel_timer.h
 * @brief   Software Timer Service for Micro-Kernel.
 *
 * Detailed Description:
 * - Architecture Role: This module provides lightweight software timers that
 * fire events to Active Objects on expiry. It enables periodic tasks (sensor
 * sampling, LED blink, watchdog feeding) and one-shot timeouts (protocol
 * response timeout, button debounce) without dedicated hardware timer channels.
 *
 * - Integration:
 * 1. Call mk_timer_init() during system setup.
 * 2. Register a post callback via mk_timer_set_post_cb() — the core provides
 *    mk_post_event() for this purpose.
 * 3. Call mk_timer_tick() from the SysTick ISR (or any 1ms periodic interrupt).
 * 4. Start timers with mk_timer_start() from any context.
 *
 * - Performance: O(n) tick processing (linear scan of MK_TIMER_MAX entries).
 *   With the default 16 timers, this completes in < 1 µs on Cortex-M.
 *
 * @note When to prefer NOT using this module (v1.x micro-kernel):
 *   If your application has no periodic tasks or timeout requirements (
 *   pure ISR-driven sensors, button polling done in hardware), the software
 *   timer module adds ~MK_TIMER_MAX × sizeof(mk_timer_t) RAM and O(n) tick
 *   overhead for no benefit. v1.x micro-kernel has no timer dependency.
 *
 * @since 2.0.0 — introduced as part of the micro-kernel v2.0 release.
 *
 * Author:   suyan
 * Date:     2026-05-28
 * Version:  2.0.0
 *****************************************************************************/

#ifndef MID_MICRO_KERNEL_TIMER_H
#define MID_MICRO_KERNEL_TIMER_H


#ifdef __cplusplus
extern "C" {
#endif


//******************************* Includes **********************************//

/* 0.come from C standard library headers */
#include <stdint.h>
#include <stdbool.h>

/* 1.come from mid layer file        */
#include "mid_micro_kernel_config.h"


//******************************* Includes **********************************//


//*********************** Types or enum or struct ***************************//

/**
 * @brief  Callback type for posting timer expiry events.
 * @param  target_id Destination AO ID.
 * @param  signal    Event signal to send.
 * @param  params    Optional payload pointer.
 */
typedef void (*mk_timer_post_cb_t)(uint8_t target_id, uint8_t signal, void *params);

/**
 * @brief  Software timer control block.
 * @details Stored in a static array managed by the timer service.
 */
typedef struct mk_timer
{
    uint8_t  target_id;     /**< AO to receive the expiry event.              */
    uint8_t  signal;        /**< Event signal sent on expiry.                 */
    uint32_t period_ms;     /**< Reload value for repeat timers (ms).         */
    uint32_t remaining_ms;  /**< Countdown until next fire.                   */
    bool     repeat;        /**< true  = periodic (auto-reload).              */
                            /**< false = one-shot (stops after firing).       */
    bool     active;        /**< true  = timer is running.                    */
} mk_timer_t;


//*********************** Types or enum or struct ***************************//


//****************************** Declaring **********************************//

/**
 * @brief  Initialize the software timer service.
 * @details Marks all timer slots as inactive. Must be called before any
 * mk_timer_start() or mk_timer_tick().
 */
void mk_timer_init(void);

/**
 * @brief  Register the callback for posting expiry events.
 * @details The timer module does not know how to deliver events; it calls
 * this callback when a timer expires. Pass mk_post_event() here.
 * @param  cb Function pointer (e.g. mk_post_event).
 */
void mk_timer_set_post_cb(mk_timer_post_cb_t cb);

/**
 * @brief  Start or restart a software timer.
 * @param  timer_id  Index into the internal timer table [0 .. MK_TIMER_MAX-1].
 * @param  target_id AO ID that will receive the expiry event.
 * @param  signal    Event signal (e.g. custom SIG_TIMER_SENSOR).
 * @param  period_ms Timer duration in milliseconds.
 * @param  repeat    true = periodic auto-reload; false = one-shot.
 */
void mk_timer_start(uint8_t timer_id, uint8_t target_id, uint8_t signal,
                    uint32_t period_ms, bool repeat);

/**
 * @brief  Stop a running timer.
 * @param  timer_id Index of the timer to stop.
 */
void mk_timer_stop(uint8_t timer_id);

/**
 * @brief  Check whether a timer is currently active.
 * @param  timer_id Index of the timer to query.
 * @return true if the timer is running, false otherwise.
 */
bool mk_timer_is_active(uint8_t timer_id);

/**
 * @brief  Timer tick handler — call from SysTick ISR (every 1 ms).
 * @details Decrements all active timers. Expired timers post their event
 * via the registered callback. One-shot timers auto-deactivate; periodic
 * timers reload.
 */
void mk_timer_tick(void);


//****************************** Declaring **********************************//


#ifdef __cplusplus
}
#endif


#endif /* MID_MICRO_KERNEL_TIMER_H */


/************************ (C) COPYRIGHT 2026 suyan *********END OF FILE*******/