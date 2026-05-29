# mid_micro_kernel — 裸机事件驱动微内核调度器

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

面向资源受限嵌入式 MCU 的轻量级协作式调度器。**纯 C99、零动态分配、单文件可移植**，专为裸机场景设计。

---

## 特性

- **双模调度** — Queue 模式（Round-Robin per-AO 队列） / Bitmap 模式（CLZ O(1) 优先级）
- **Per-AO 事件隔离** — 每个 Active Object 有独立 SPSC 锁队列，互为灾难隔离
- **多 ISR 安全** — PRIMASK 临界区保护，允许多个中断源并发投递事件
- **零栈协程** — Protothreads 引擎，`uint16_t` 状态开销，没有独立栈
- **软件定时器** — 周期 / 单次定时，到期自动投递事件
- **阻塞延时宏** — `MK_PT_DELAY(ms)`，49.7 天溢出安全
- **遥测 & 故障检测** — 执行耗时打点 + 超时告警回调
- **低功耗空闲** — 无事件时自动 WFI 睡眠
- **跨编译器** — GCC / Clang / Keil ARMCC 三套硬件抽象宏

---

## 快速开始

### 1. 复制源码

将 `src/` 目录下的全部 `.h` + `.c` 文件复制到你的 MCU 工程中。

### 2. 包含头文件

```c
#include "mid_micro_kernel_core.h"
#include "mid_micro_kernel_timer.h"
```

### 3. 定义 Active Objects

```c
typedef struct {
    mk_active_object_t  super;     /* 必须作为第一个成员 */   
    int                 my_state;
} app_ao_t;

static int my_dispatch(mk_active_object_t *ao, mk_event_msg_t *evt)
{
    app_ao_t *self = (app_ao_t *)ao;
    MK_PT_BEGIN(&ao->pt_state);

    /* AO 的业务逻辑 */
    MK_PT_WAIT_UNTIL(&ao->pt_state, self->my_state == READY);
    mk_post_event(ANOTHER_AO_ID, SIG_START, NULL);

    MK_PT_END(&ao->pt_state);
}

static app_ao_t my_ao = {
    .super.active_object_id = 0,
    .super.pf_dispatch     = my_dispatch,
};
```

### 4. 初始化 & 运行

```c
int main(void)
{
    mk_scheduler_init();
    mk_timer_init();
    mk_timer_set_post_cb(mk_post_event);
    mk_set_tick_callback(hw_get_ms_tick);

    mk_register_ao((mk_active_object_t *)&my_ao);
    mk_timer_start(0, MY_AO_ID, SIG_PERIODIC, 100, true);

    mk_scheduler_run();     /* 永不返回 */
    return 0;
}
```

### 5. SysTick ISR

```c
void SysTick_Handler(void)
{
    mk_timer_tick();        /* 驱动软件定时器 */
}
```

---

## 目录结构

```
mid_micro_kernel/
├── README.md               ← 本文件
├── LICENSE                 ← MIT 许可证
├── CHANGELOG.md            ← 版本历史
├── src/                    ← 内核源码（全部 9 个文件）
│   ├── mid_micro_kernel_config.h
│   ├── mid_micro_kernel_core.h / .c
│   ├── mid_micro_kernel_type.h
│   ├── mid_micro_kernel_queue.h / .c
│   ├── mid_micro_kernel_pt.h
│   └── mid_micro_kernel_timer.h / .c
├── test/                   ← PC 端单元测试
│   ├── test_all.c
│   ├── test_config.h
│   ├── test_report.md
│   └── README.md
├── examples/               ← 示例工程
│   └── blinky/
└── docs/                   ← 文档
    └── architecture.md
```

---

## 测试

```bash
cd test
gcc -I../src -Wall -Wextra -o test_all test_all.c
.\test_all
```

（详情见 [test/README.md](test/README.md) 和 [test_report.md](test/test_report.md)）

---

## 许可证

MIT License — 详见 [LICENSE](LICENSE)。

## 作者

suyan — 2026
