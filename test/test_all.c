/******************************************************************************
 * @file    test_all.c
 * @brief   Host-based unit tests for the Micro-Kernel v2.0.
 *
 * @details
 * Compile and run on a host PC (no Cortex-M required):
 *   gcc -I.. -Wall -Wextra -o test_all test_all.c && ./test_all
 *
 * Each test group prints PASS/FAIL and returns a summary exit code.
 *****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <assert.h>

/* ── 1. Test configuration (override hardware macros for host) ── */
#include "test_config.h"

/* ── 2. Tell config.h and type.h we're testing (no __GNUC__ paths) ── */
/*    test_config.h already undef'd MK_* macros and overrode them.     */

/* ── 3. Include production sources as a single translation unit ── */
/*    This gives us access to static variables for white-box checks.  */
#include "../src/mid_micro_kernel_queue.c"
#include "../src/mid_micro_kernel_core.c"
#include "../src/mid_micro_kernel_timer.c"


/* ================================================================== *
 *  Test helpers
 * ================================================================== */

static int  s_tests_passed = 0;
static int  s_tests_failed = 0;

#define TEST_GROUP(name)       printf("\n=== %s ===\n", name)
#define TEST(name)             printf("  %-48s ", name)
#define PASS()                 do { s_tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg)              do { s_tests_failed++; printf("FAIL: %s\n", msg); } while(0)
#define ASSERT(cond, msg)      do { if (!(cond)) { FAIL(msg); return; } } while(0)
#define ASSERT_EQ(a, b, msg)   do { if ((a) != (b)) { FAIL(msg); return; } } while(0)

/* Mock tick — we control the return value for PT_DELAY and telemetry tests */
static uint32_t s_mock_tick = 0;
static uint32_t test_get_tick(void) { return s_mock_tick; }

/* Test Active Object used for scheduler / dispatch tests */
typedef struct {
    mk_active_object_t  super;        /* "inherits" mk_active_object_t    */
    int                 last_signal;  /* records the last signal received */
    uint32_t            last_params;  /* records the params pointer value  */
} test_ao_t;

/* Shared event counter for broadcast tests */
static int s_broadcast_count = 0;

static int test_ao_dispatch(mk_active_object_t *ao, mk_event_msg_t *evt)
{
    test_ao_t *self = (test_ao_t *)ao;

    MK_PT_BEGIN(&ao->pt_state);

    self->last_signal = evt->event_signal;
    self->last_params = (uint32_t)(uintptr_t)evt->params;

    /* A simple delay to test MK_PT_DELAY */
    MK_PT_DELAY(ao, 5);
    /* After 5 ms we arrive here — signal that the delay passed */
    self->last_signal = 0xFF;

    MK_PT_END(&ao->pt_state);

    return PT_ENDED;
}

static int test_ao_broadcast(mk_active_object_t *ao, mk_event_msg_t *evt)
{
    (void)ao;
    (void)evt;
    s_broadcast_count++;
    return PT_ENDED;
}


/* ================================================================== *
 *  Queue tests
 * ================================================================== */

static void test_queue_basic(void)
{
    mk_queue_t        q;
    mk_event_msg_t    buf[8];
    mk_event_msg_t    in, out;

    mk_queue_init(&q, buf, 8);

    /* Empty dequeue should fail */
    ASSERT(mk_queue_dequeue(&q, &out) == MK_QUEUE_EMPTY, "empty dequeue");

    /* Enqueue → dequeue should match */
    in.target_id = 5;
    in.event_signal = 0x42;
    in.params = (void *)0x1234;
    ASSERT(mk_queue_enqueue(&q, &in) == MK_QUEUE_OK, "enqueue ok");
    ASSERT(mk_queue_dequeue(&q, &out) == MK_QUEUE_OK, "dequeue ok");
    ASSERT_EQ(out.target_id,    5,     "target_id");
    ASSERT_EQ(out.event_signal, 0x42,  "event_signal");
    ASSERT_EQ((uintptr_t)out.params, (uintptr_t)0x1234, "params");

    PASS();
}

static void test_queue_full_empty(void)
{
    mk_queue_t        q;
    mk_event_msg_t    buf[4];    /* capacity = 4 */
    mk_event_msg_t    in, out;
    int               i;

    mk_queue_init(&q, buf, 4);

    /* Fill queue: capacity-1 entries (SPSC uses one slot as sentinel) */
    for (i = 0; i < 3; i++) {
        in.target_id = (uint8_t)i;
        ASSERT(mk_queue_enqueue(&q, &in) == MK_QUEUE_OK, "fill enqueue");
    }
    /* Next enqueue should fail — queue full */
    in.target_id = 99;
    ASSERT(mk_queue_enqueue(&q, &in) == MK_QUEUE_FULL, "full detect");

    /* Drain */
    for (i = 0; i < 3; i++) {
        ASSERT(mk_queue_dequeue(&q, &out) == MK_QUEUE_OK, "drain dequeue");
    }
    ASSERT(mk_queue_dequeue(&q, &out) == MK_QUEUE_EMPTY, "empty after drain");

    PASS();
}

static void test_queue_wraparound(void)
{
    mk_queue_t        q;
    mk_event_msg_t    buf[4];
    mk_event_msg_t    in, out;
    int               i;

    mk_queue_init(&q, buf, 4);

    /* Simulate wrap: enqueue 3, dequeue 2 (rd_idx=2, wr_idx=3) */
    for (i = 0; i < 3; i++) {
        in.target_id = (uint8_t)(100 + i);
        mk_queue_enqueue(&q, &in);
    }
    mk_queue_dequeue(&q, &out);   /* drop 100 */
    mk_queue_dequeue(&q, &out);   /* drop 101 */
    /* Now rd_idx=2, wr_idx=3 (one slot used) */
    /* Enqueue 4 more; second one should wrap the write index */
    for (i = 0; i < 2; i++) {
        in.target_id = (uint8_t)(200 + i);
        ASSERT(mk_queue_enqueue(&q, &in) == MK_QUEUE_OK, "wrap enqueue");
    }
    /* Should be full now */
    ASSERT(mk_queue_enqueue(&q, &in) == MK_QUEUE_FULL, "full after wrap");

    /* Read back in FIFO order: 102, 200, 201 */
    ASSERT(mk_queue_dequeue(&q, &out) == MK_QUEUE_OK, "wrap read 1");
    ASSERT_EQ(out.target_id, 102, "wrap val 1");
    ASSERT(mk_queue_dequeue(&q, &out) == MK_QUEUE_OK, "wrap read 2");
    ASSERT_EQ(out.target_id, 200, "wrap val 2");
    ASSERT(mk_queue_dequeue(&q, &out) == MK_QUEUE_OK, "wrap read 3");
    ASSERT_EQ(out.target_id, 201, "wrap val 3");
    ASSERT(mk_queue_dequeue(&q, &out) == MK_QUEUE_EMPTY, "empty after wrap");

    PASS();
}

static void test_queue_fifo_order(void)
{
    mk_queue_t        q;
    mk_event_msg_t    buf[8];
    mk_event_msg_t    in, out;

    mk_queue_init(&q, buf, 8);

    for (int i = 0; i < 5; i++) {
        in.target_id = (uint8_t)i;
        mk_queue_enqueue(&q, &in);
    }
    for (int i = 0; i < 5; i++) {
        ASSERT(mk_queue_dequeue(&q, &out) == MK_QUEUE_OK, "order dequeue");
        ASSERT_EQ(out.target_id, (uint8_t)i, "FIFO order");
    }

    PASS();
}

static void test_queue_is_empty(void)
{
    mk_queue_t        q;
    mk_event_msg_t    buf[8];
    mk_event_msg_t    in, out;

    mk_queue_init(&q, buf, 8);
    ASSERT(mk_queue_is_empty(&q), "empty initially");

    mk_queue_enqueue(&q, &in);
    ASSERT(!mk_queue_is_empty(&q), "not empty after enqueue");

    mk_queue_dequeue(&q, &out);
    ASSERT(mk_queue_is_empty(&q), "empty after dequeue");

    PASS();
}


/* ================================================================== *
 *  Timer tests
 * ================================================================== */

/* Recording callback: records how many times it was called */
static int   s_post_count = 0;
static int   s_post_last_target = -1;
static int   s_post_last_signal = -1;
static void test_post_cb(uint8_t target, uint8_t sig, void *p)
{
    (void)p;
    s_post_count++;
    s_post_last_target = (int)target;
    s_post_last_signal = (int)sig;
}

static void test_timer_init(void)
{
    mk_timer_init();
    mk_timer_set_post_cb(test_post_cb);
    ASSERT(!mk_timer_is_active(0), "inactive after init");
    PASS();
}

static void test_timer_one_shot(void)
{
    mk_timer_init();
    mk_timer_set_post_cb(test_post_cb);
    s_post_count = 0;

    mk_timer_start(0, 3, 0xAA, 10, false);
    ASSERT(mk_timer_is_active(0), "active after start");

    /* Tick 9 times — not yet expired */
    for (int i = 0; i < 9; i++) mk_timer_tick();
    ASSERT_EQ(s_post_count, 0, "not yet fired");
    ASSERT(mk_timer_is_active(0), "still active before expiry");

    /* Tick 10 — should fire */
    mk_timer_tick();
    ASSERT_EQ(s_post_count, 1, "fired once");
    ASSERT_EQ(s_post_last_target, 3, "target id");
    ASSERT_EQ(s_post_last_signal, 0xAA, "signal");
    ASSERT(!mk_timer_is_active(0), "one-shot stopped after fire");

    /* Tick again — should NOT fire again */
    mk_timer_tick();
    ASSERT_EQ(s_post_count, 1, "only fired once");

    PASS();
}

static void test_timer_periodic(void)
{
    mk_timer_init();
    mk_timer_set_post_cb(test_post_cb);
    s_post_count = 0;

    mk_timer_start(1, 7, 0xBB, 5, true);
    ASSERT(mk_timer_is_active(1), "active after start");

    /* After 5th tick: first fire */
    for (int i = 0; i < 5; i++) mk_timer_tick();
    ASSERT_EQ(s_post_count, 1, "periodic 1st fire");

    /* After another 5 ticks: second fire */
    for (int i = 0; i < 5; i++) mk_timer_tick();
    ASSERT_EQ(s_post_count, 2, "periodic 2nd fire");

    /* After another 5: third fire */
    for (int i = 0; i < 5; i++) mk_timer_tick();
    ASSERT_EQ(s_post_count, 3, "periodic 3rd fire");

    ASSERT(mk_timer_is_active(1), "still active after multiple fires");

    PASS();
}

static void test_timer_stop(void)
{
    mk_timer_init();
    mk_timer_set_post_cb(test_post_cb);
    s_post_count = 0;

    mk_timer_start(2, 5, 0xCC, 10, true);
    mk_timer_stop(2);
    ASSERT(!mk_timer_is_active(2), "stopped");

    /* Tick 20 times — should not fire */
    for (int i = 0; i < 20; i++) mk_timer_tick();
    ASSERT_EQ(s_post_count, 0, "no fire after stop");

    PASS();
}

static void test_timer_multiple_simultaneous(void)
{
    mk_timer_init();
    mk_timer_set_post_cb(test_post_cb);
    s_post_count = 0;

    /* Two timers, both expiring at tick 10 */
    mk_timer_start(0, 1, 0xA1, 10, true);
    mk_timer_start(1, 2, 0xB2, 10, true);

    for (int i = 0; i < 9; i++) mk_timer_tick();
    ASSERT_EQ(s_post_count, 0, "neither fired yet");

    mk_timer_tick();  /* both expire simultaneously */
    ASSERT_EQ(s_post_count, 2, "both fired");

    PASS();
}


/* ================================================================== *
 *  Scheduler tests
 * ================================================================== */

static void test_scheduler_init(void)
{
    mk_scheduler_init();
    /* s_ao_table should be all NULL after init */
    for (int i = 0; i < AO_MAX; i++) {
        ASSERT(s_ao_table[i] == 0, "ao_table cleared");
    }

    mk_set_tick_callback(test_get_tick);
    mk_set_timeout_threshold(1000);  /* 1s — won't trip in tests */

    PASS();
}

static void test_register_ao(void)
{
    test_ao_t ao1, ao2;

    mk_scheduler_init();

    memset(&ao1, 0, sizeof(ao1));
    ao1.super.active_object_id = 5;
    ao1.super.pf_dispatch = test_ao_dispatch;

    memset(&ao2, 0, sizeof(ao2));
    ao2.super.active_object_id = 10;
    ao2.super.pf_dispatch = test_ao_dispatch;

    mk_register_ao((mk_active_object_t *)&ao1);
    ASSERT(s_ao_table[5] == (mk_active_object_t *)&ao1, "ao1 registered");

    mk_register_ao((mk_active_object_t *)&ao2);
    ASSERT(s_ao_table[10] == (mk_active_object_t *)&ao2, "ao2 registered");

    /* Unregistered slots should stay NULL */
    ASSERT(s_ao_table[0]  == 0, "slot 0 empty");
    ASSERT(s_ao_table[15] == 0, "slot 15 empty");

    /* Queue should be initialized */
    ASSERT(ao1.super.queue.buffer == ao1.super.queue_storage, "queue buffer linked");
    ASSERT(ao1.super.queue.mask == MK_AO_QUEUE_CAPACITY - 1, "queue mask correct");

    PASS();
}

static void test_post_event(void)
{
    test_ao_t ao;
    mk_event_msg_t evt;

    mk_scheduler_init();
    mk_set_tick_callback(test_get_tick);

    memset(&ao, 0, sizeof(ao));
    ao.super.active_object_id = 2;
    ao.super.pf_dispatch = test_ao_dispatch;
    mk_register_ao((mk_active_object_t *)&ao);

    /* Post an event */
    mk_post_event(2, 0x77, (void *)0xDEAD);

    /* Check the AO's queue directly */
    ASSERT(mk_queue_dequeue(&ao.super.queue, &evt) == MK_QUEUE_OK, "event queued");
    ASSERT_EQ(evt.target_id,    2,     "post target_id");
    ASSERT_EQ(evt.event_signal, 0x77,  "post signal");
    ASSERT_EQ((uintptr_t)evt.params, (uintptr_t)0xDEAD, "post params");

    PASS();
}

static void test_post_event_invalid_id(void)
{
    test_ao_t ao;
    mk_event_msg_t evt;

    mk_scheduler_init();

    memset(&ao, 0, sizeof(ao));
    ao.super.active_object_id = 0;
    ao.super.pf_dispatch = test_ao_dispatch;
    mk_register_ao((mk_active_object_t *)&ao);

    /* Post to invalid ID — should be silently dropped */
    mk_post_event(0xFF, 0x99, 0);

    /* AO's queue should still be empty */
    ASSERT(mk_queue_dequeue(&ao.super.queue, &evt) == MK_QUEUE_EMPTY, "invalid id dropped");

    PASS();
}

static void test_post_broadcast(void)
{
    test_ao_t aos[3];
    mk_event_msg_t evt;

    mk_scheduler_init();
    mk_set_tick_callback(test_get_tick);

    for (int i = 0; i < 3; i++) {
        memset(&aos[i], 0, sizeof(aos[i]));
        aos[i].super.active_object_id = (uint8_t)i;
        aos[i].super.pf_dispatch = test_ao_dispatch;
        mk_register_ao((mk_active_object_t *)&aos[i]);
    }

    /* Also register a broadcast-specific AO */
    test_ao_t bcast_ao;
    memset(&bcast_ao, 0, sizeof(bcast_ao));
    bcast_ao.super.active_object_id = 7;
    bcast_ao.super.pf_dispatch = test_ao_broadcast;
    mk_register_ao((mk_active_object_t *)&bcast_ao);

    s_broadcast_count = 0;
    mk_post_broadcast(0x55, 0);

    /* All four registered AOs should have an event in their queue */
    for (int i = 0; i < 3; i++) {
        ASSERT(mk_queue_dequeue(&aos[i].super.queue, &evt) == MK_QUEUE_OK, "bcast to ao");
    }
    ASSERT(mk_queue_dequeue(&bcast_ao.super.queue, &evt) == MK_QUEUE_OK, "bcast to extra");
    ASSERT(mk_queue_dequeue(&bcast_ao.super.queue, &evt) == MK_QUEUE_EMPTY, "only one event per ao");

    PASS();
}


/* ================================================================== *
 *  MK_PT_DELAY wrap-around test (the uint32_t overflow fix)
 * ================================================================== */

/* A minimal dispatch function that just uses MK_PT_DELAY.
 * We drive it externally by calling it with different mock tick values. */
typedef struct {
    mk_active_object_t  super;
    int                 delay_passed;
} delay_test_ao_t;

static int delay_dispatch(mk_active_object_t *ao, mk_event_msg_t *evt)
{
    delay_test_ao_t *self = (delay_test_ao_t *)ao;
    (void)evt;

    MK_PT_BEGIN(&ao->pt_state);

    self->delay_passed = 0;
    MK_PT_DELAY(ao, 100);       /* Wait 100 ms */
    self->delay_passed = 1;

    MK_PT_END(&ao->pt_state);

    return PT_ENDED;
}

static void test_pt_delay_normal(void)
{
    delay_test_ao_t dao;
    mk_event_msg_t  dummy = {0, 0, 0};

    mk_scheduler_init();
    memset(&dao, 0, sizeof(dao));
    dao.super.active_object_id = 1;
    dao.super.pf_dispatch = delay_dispatch;
    mk_register_ao((mk_active_object_t *)&dao);

    /* First call: current tick = 1000, deadline = 1100 */
    s_mock_tick = 1000;
    int rc = dao.super.pf_dispatch((mk_active_object_t *)&dao, &dummy);
    ASSERT_EQ(rc, PT_WAITING, "delay: should wait");
    ASSERT_EQ(dao.delay_passed, 0, "delay: not passed yet");
    ASSERT_EQ(dao.super.delay_deadline, 1100U, "delay: deadline = 1100");

    /* Second call: tick = 1050, still < 1100 → still waiting */
    s_mock_tick = 1050;
    rc = dao.super.pf_dispatch((mk_active_object_t *)&dao, &dummy);
    ASSERT_EQ(rc, PT_WAITING, "delay: still waiting at t=1050");
    ASSERT_EQ(dao.delay_passed, 0, "delay: still not passed");

    /* Third call: tick = 1100, now >= deadline → passes */
    s_mock_tick = 1100;
    rc = dao.super.pf_dispatch((mk_active_object_t *)&dao, &dummy);
    ASSERT_EQ(rc, PT_ENDED, "delay: passed");
    ASSERT_EQ(dao.delay_passed, 1, "delay: passed flag set");

    PASS();
}

static void test_pt_delay_wrap_around(void)
{
    delay_test_ao_t dao;
    mk_event_msg_t  dummy = {0, 0, 0};

    mk_scheduler_init();
    memset(&dao, 0, sizeof(dao));
    dao.super.active_object_id = 2;
    dao.super.pf_dispatch = delay_dispatch;
    mk_register_ao((mk_active_object_t *)&dao);

    /* ── Simulate tick near wrap point ── */
    /* tick = 0xFFFFFFF0, we ask for 100ms delay.
     * deadline = 0xFFFFFFF0 + 100 = 0x00000054 (wraps around!) */

    s_mock_tick = 0xFFFFFFF0;
    int rc = dao.super.pf_dispatch((mk_active_object_t *)&dao, &dummy);
    ASSERT_EQ(rc, PT_WAITING, "wrap: should wait at t=0xFFFFFFF0");
    ASSERT_EQ(dao.super.delay_deadline, (uint32_t)0x00000054, "wrap: deadline wrapped");

    /* ── tick = 0xFFFFFFF5, still before the wrapped deadline ──
     * Without the fix: 0xFFFFFFF5 < 0x00000054 → FALSE → prematurely ends.
     * With the fix:     (int32_t)(0xFFFFFFF5 - 0x00000054) = -95 < 0 → WAITING ✓ */
    s_mock_tick = 0xFFFFFFF5;
    rc = dao.super.pf_dispatch((mk_active_object_t *)&dao, &dummy);
    ASSERT_EQ(rc, PT_WAITING, "wrap: still waiting at t=0xFFFFFFF5");
    ASSERT_EQ(dao.delay_passed, 0, "wrap: should not have passed yet");

    /* ── tick = 0x00000055 (deadline + 1) → should pass ──
     * (int32_t)(0x00000055 - 0x00000054) = +1 >= 0 → elapsed ✓ */
    s_mock_tick = 0x00000055;
    rc = dao.super.pf_dispatch((mk_active_object_t *)&dao, &dummy);
    ASSERT_EQ(rc, PT_ENDED, "wrap: passed after deadline");
    ASSERT_EQ(dao.delay_passed, 1, "wrap: passed flag set");

    PASS();
}


/* ================================================================== *
 *  Test runner
 * ================================================================== */

int main(void)
{
    printf("Micro-Kernel v2.0 — Host Unit Tests\n");
    printf("Scheduler mode: %s\n\n",
        (MK_CFG_SCHEDULER_MODE == MK_MODE_QUEUE) ? "QUEUE" :
        (MK_CFG_SCHEDULER_MODE == MK_MODE_BITMAP) ? "BITMAP" : "???");

    /* ── Queue tests ── */
    TEST_GROUP("Queue");
    test_queue_basic();
    test_queue_full_empty();
    test_queue_wraparound();
    test_queue_fifo_order();
    test_queue_is_empty();

    /* ── Timer tests ── */
    TEST_GROUP("Timer");
    test_timer_init();
    test_timer_one_shot();
    test_timer_periodic();
    test_timer_stop();
    test_timer_multiple_simultaneous();

    /* ── Scheduler tests ── */
    TEST_GROUP("Scheduler");
    test_scheduler_init();
    test_register_ao();
    test_post_event();
    test_post_event_invalid_id();
    test_post_broadcast();

    /* ── PT_DELAY wrap-around tests ── */
    TEST_GROUP("MK_PT_DELAY");
    test_pt_delay_normal();
    test_pt_delay_wrap_around();

    /* ── Summary ── */
    printf("\n========================\n");
    printf("  Passed: %d\n", s_tests_passed);
    printf("  Failed: %d\n", s_tests_failed);
    printf("========================\n");

    return (s_tests_failed > 0) ? 1 : 0;
}
