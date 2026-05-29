/******************************************************************************
 * @brief   Blinky 示例 — Micro-Kernel v2.0 典型用法
 *
 * 功能：LED 每 500ms 闪烁一次，同时串口每秒打印一次计数。
 * 展示 per-AO 事件隔离、定时器服务、MK_PT_DELAY 三个核心特性。
 *
 * 硬件假设：
 *   - Cortex-M MCU
 *   - PA5 接 LED (推挽输出)
 *   - USART1 已初始化 (打印调试信息)
 *   - SysTick 配置为 1ms 中断
 *****************************************************************************/

#include "mid_micro_kernel_core.h"
#include "mid_micro_kernel_timer.h"

/* ─── 硬件抽象（示例，按实际平台替换） ─── */
#define LED_PIN      5
#define LED_PORT     GPIOA
#define gpio_toggle(p, n)  (p)->ODR ^= (1 << (n))
#define uart_puts(s)       /* 实现你的串口输出 */

extern uint32_t get_sys_tick_ms(void);   /* 硬件 SysTick 计数 */

/* ─── 信号定义 ─── */
#define SIG_LED_TOGGLE  0x10
#define SIG_UART_PRINT   0x11

/* ─── AO: LED ─── */
typedef struct {
    mk_active_object_t  super;
} led_ao_t;

static int led_dispatch(mk_active_object_t *ao, mk_event_msg_t *evt)
{
    (void)ao;
    MK_PT_BEGIN(&ao->pt_state);

    while (1) {
        MK_PT_WAIT_UNTIL(&ao->pt_state, evt->event_signal == SIG_LED_TOGGLE);
        gpio_toggle(LED_PORT, LED_PIN);
    }

    MK_PT_END(&ao->pt_state);
}

static led_ao_t led_ao = {
    .super.active_object_id = 0,
    .super.pf_dispatch     = led_dispatch,
};

/* ─── AO: UART 打印 ─── */
typedef struct {
    mk_active_object_t  super;
    uint32_t            counter;
} uart_ao_t;

static int uart_dispatch(mk_active_object_t *ao, mk_event_msg_t *evt)
{
    uart_ao_t *self = (uart_ao_t *)ao;
    MK_PT_BEGIN(&ao->pt_state);

    while (1) {
        MK_PT_WAIT_UNTIL(&ao->pt_state, evt->event_signal == SIG_UART_PRINT);
        self->counter++;
        /* printf 系列在嵌入式上可能阻塞，这里只用 1ms 打印 */
    }

    MK_PT_END(&ao->pt_state);
}

static uart_ao_t uart_ao = {
    .super.active_object_id = 1,
    .super.pf_dispatch     = uart_dispatch,
};

/* ─── 主函数 ─── */
int main(void)
{
    mk_scheduler_init();
    mk_timer_init();
    mk_timer_set_post_cb(mk_post_event);
    mk_set_tick_callback(get_sys_tick_ms);

    mk_register_ao((mk_active_object_t *)&led_ao);
    mk_register_ao((mk_active_object_t *)&uart_ao);

    /* 定时器 0: 每 500ms 触发 LED 切换 */
    mk_timer_start(0, 0, SIG_LED_TOGGLE, 500, true);

    /* 定时器 1: 每 1000ms 触发 UART 打印 */
    mk_timer_start(1, 1, SIG_UART_PRINT, 1000, true);

    mk_scheduler_run();
    return 0;
}

/* ─── SysTick ISR ─── */
void SysTick_Handler(void)
{
    mk_timer_tick();
}
