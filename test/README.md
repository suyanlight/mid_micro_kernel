# Micro-Kernel v2.0 — Host Unit Tests

## 前置条件

任何带 C99 编译器的开发机 (gcc / clang / MinGW)。

## 编译 & 运行

```bash
cd test
gcc -I../src -Wall -Wextra -o test_all test_all.c
 .\test_all
```

或者对 C++ 项目同样适用：

```bash
g++ -I../src -Wall -Wextra -Werror -o test_all test_all.c && ./test_all
```

## 测试覆盖

| 测试组 | 文件内函数 | 测试项 |
|---|---|---|
| **Queue** | `mk_queue_init/enqueue/dequeue/is_empty` | 基本入出队、满空检测、位掩码绕回、FIFO 顺序、空队列判断 |
| **Timer** | `mk_timer_init/start/stop/tick/is_active` | 单次到期、周期重载、停止取消、同到期竞争 |
| **Scheduler** | `mk_scheduler_init/register_ao/post_event/post_broadcast` | 注册验证、事件投递、非法 ID 丢弃、广播 |
| **PT_DELAY** | `MK_PT_DELAY` 宏 | 正常延时、**uint32_t 溢出回绕场景**（49.7 天边界） |

## 测试设计原则

- **零外部依赖** — 只用 `<stdio.h>`、`<string.h>`、`<assert.h>`，没有 cmocka / CUnit
- **单翻译单元** — `#include` 生产源码文件，白盒访问内部状态 (`s_ao_table`、`s_irq_depth`)
- **硬件宏屏蔽** — `test_config.h` 将所有 Cortex-M 指令替换为 no-op + 跟踪计数器 `s_irq_depth`
- **可复现** — 所有时序依赖通过 `s_mock_tick` 人工驱动，不依赖真实时钟

## 边界测试亮点

`test_pt_delay_wrap_around()` 模拟了 `mk_get_tick()` 在 49.7 天连续运行后回绕到零的场景：

```
tick = 0xFFFFFFF0  →  delay(100ms) →  deadline = 0x00000054  (回绕)
tick = 0xFFFFFFF5  →  直接比: 0xFFFFFFF5 < 0x00000054 → FALSE → BUG
                     带符号差: (int32_t)(-95) < 0       → TRUE  → ✅ 正确等待
tick = 0x00000055  →  超过 deadline 1ms → 延时结束
```

## 添加新测试

```c
static void test_my_feature(void)
{
    TEST("my feature does X");
    /* arrange + act */
    ASSERT_EQ(actual, expected, "explain what broke");
    PASS();
}

int main(void) {
    /* ... */
    TEST_GROUP("My Feature");
    test_my_feature();
    /* ... */
}
```

## 测试结果

```bash
PS C:\Users\sua94\Desktop\mid_micro_kernel\test> gcc -I../src -Wall -Wextra -o test_all test_all.c
PS C:\Users\sua94\Desktop\mid_micro_kernel\test> .\test_all
Micro-Kernel v2.0 鈥?Host Unit Tests
Scheduler mode: QUEUE


=== Queue ===
PASS
PASS
PASS
PASS
PASS

=== Timer ===
PASS
PASS
PASS
PASS
PASS

=== Scheduler ===
PASS
PASS
PASS
PASS
PASS

=== MK_PT_DELAY ===
PASS
PASS

========================
  Passed: 17
  Failed: 0
========================
```

