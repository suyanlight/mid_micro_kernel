# Micro-Kernel v2.0 — PC 端单元测试报告

> **测试日期**：2026-05-28  
> **测试环境**：Windows 11 + MinGW GCC (x86_64)  
> **编译命令**：`gcc -I.. -Wall -Wextra -Werror -o test_all test_all.c`  
> **被测对象**：mid_micro_kernel v2.0.0（queue / core / timer / pt 四模块）

---

## 一、测试目标

验证 v2.0 版本在**裸机场景下 85% 覆盖能力**所依赖的核心基础设施是否正确运行，重点关注：

| 关注点 | 为什么重要 |
|---|---|
| **Per-AO 队列的正确性** | v1.0 → v2.0 最大的架构变化：从全局单队列改为每 AO 独立队列。入队/出队/绕回/满空判断全错就会导致事件丢失或乱序 |
| **多生产者安全** | `MK_DISABLE_IRQ / MK_ENABLE_IRQ` 在 enqueue 路径上的保护是否生效（通过 `s_irq_depth` 追踪） |
| **软件定时器到期行为** | 周期任务和超时检测是裸机场景最高频的需求，单次/周期/停止/同时到期是否正确 |
| **事件投递链路** | `mk_post_event → mk_queue_enqueue → mk_queue_dequeue` 是否完整 |
| **MK_PT_DELAY 溢出修复** | `uint32_t` tick 计数器在 49.7 天回绕后，延时判断是否会错误提前结束 |

---

## 二、测试用例与结果

### 2.1 Queue — 队列模块（5/5 ✅）

| 用例 | 操作 | 期望 | 结果 |
|---|---|---|---|
| **基本入出队** | enqueue(5, 0x42, 0x1234) → dequeue | 读回相同内容 | ✅ PASS |
| **满/空检测** | 填满容量-1 → 再入队返回 FULL → 排空 → 再出队返回 EMPTY | FULL/EMPTY 正确 | ✅ PASS |
| **位掩码绕回** | enqueue 3 个 → dequeue 2 个(rd=2,wr=3) → enqueue 2 个(绕回 rd 位置) → 读回 FIFO 顺序 | 102, 200, 201 | ✅ PASS |
| **FIFO 顺序** | 入队 0-4 → 出队 5 次 | 顺序不变 | ✅ PASS |
| **is_empty** | init→空→入队→非空→出队→空 | 布尔值正确 | ✅ PASS |

**说明**：队列是 SPSC 环形缓冲区的经典实现，测试覆盖了全部边界条件。绕回测试确认了 `(wr+1) & mask` 在索引跨越数组末尾后仍能正确寻址。

### 2.2 Timer — 定时器模块（5/5 ✅）

| 用例 | 操作 | 期望 | 结果 |
|---|---|---|---|
| **初始化** | `mk_timer_init()` → `is_active(0)` | false | ✅ PASS |
| **单次到期** | start(period=10) → tick×9 → tick×1 | 第 10 个 tick 触发一次，之后停止 | ✅ PASS |
| **周期重载** | start(period=5, repeat) → tick×5×3 | 每 5 个 tick 触发一次，共 3 次 | ✅ PASS |
| **停止取消** | start(period=10) → stop → tick×20 | 不触发 | ✅ PASS |
| **同时到期** | 两个定时器同周期 → 同 tick 触发 | 两个回调都被调用 | ✅ PASS |

**说明**：定时器是纯数组线性扫描，O(n) 复杂度，测试确认了到期→回调→重载/停止的生命周期完整。同时到期场景验证了多个定时器在同一次 `mk_timer_tick()` 中都能正确触发，不存在一个触发后覆盖另一个的问题。

### 2.3 Scheduler — 调度器核心（5/5 ✅）

| 用例 | 操作 | 期望 | 结果 |
|---|---|---|---|
| **初始化清表** | `mk_scheduler_init()` | `s_ao_table` 全 NULL | ✅ PASS |
| **AO 注册** | register 两个 AO 到 id=5 和 id=10 | 对应槽位非空，其余槽位 NULL，队列已初始化 | ✅ PASS |
| **事件投递** | `mk_post_event(2, 0x77, DEAD)` → 检查 AO#2 队列 | 队列中有匹配事件 | ✅ PASS |
| **非法 ID 丢弃** | `mk_post_event(0xFF, ...)` → 检查 AO#0 队列 | 队列空（事件被丢弃） | ✅ PASS |
| **广播** | `mk_post_broadcast(0x55)` → 检查 4 个注册 AO | 每 AO 一个事件，无多余事件 | ✅ PASS |

**说明**：调度器核心链路（注册→投递→队列缓冲）经验证完整。`mk_post_event` 在 `target_id >= AO_MAX` 时静默返回而不崩溃，这是设计要求。

### 2.4 MK_PT_DELAY — 溢出修复验证（2/2 ✅）

| 用例 | 操作 | 期望 | 结果 |
|---|---|---|---|
| **正常延时** | mock_tick=1000 → dispatch → 等待 → mock_tick=1100 → dispatch | 第 1 次 WAITING，第 2 次通过 | ✅ PASS |
| **49.7 天溢出回绕** | mock_tick=0xFFFFFFF0 → delay(100ms) → deadline 回绕到 0x54 → mock_tick=0xFFFFFFF5 → dispatch → mock_tick=0x55 → dispatch | 回绕后仍然等待，最终通过 | ✅ PASS |

**说明**：这是本报告最重要的测试（详见下文第 3 节）。

---

## 三、关键发现

### 3.1 🐛 发现并修复：`s_ready_tasks_map` 位图 RMW 竞态

**发现途径**：代码审查（非测试，该变量在线程/ISR 并发路径上无法通过单线程测试暴露）

**问题**：`mk_set_task_ready()`、`mk_post_event()` 的 Bitmap 分支、`mk_scheduler_run()` 的 Bitmap 分支三者都对 `s_ready_tasks_map` 做读-改-写操作，但没有任何保护。ISR 的 `|=` 与后台的 `&=~` 在汇编层面是 LDR → ORR/BIC → STR 三步，中断可能在中间打断导致 ISR 的 bit 被后台的旧值写回覆盖。

**修复**：三处操作全部加 `MK_DISABLE_IRQ() / MK_ENABLE_IRQ()` 保护。

### 3.2 🐛 发现并修复：`MK_PT_DELAY` uint32_t 溢出回绕

**发现途径**：代码审查 + 测试 `test_pt_delay_wrap_around` 验证修复

**问题**：`if (mk_get_tick() < delay_deadline)` 在 deadline 回绕而 tick 尚未回绕时恒为 false，导致延时几乎立即结束。

**复现条件**：系统连续运行约 49.7 天（`2^32 × 1ms`）后 tick 回绕。修复前：

```
deadline = 0xFFFFFFF0 + 100(ms) = 0x00000054 (回绕)
tick = 0xFFFFFFF5
直接比较: 0xFFFFFFF5 < 0x00000054 → FALSE → ❌ 误判超时
```

**修复**：改为 `(int32_t)(current - deadline) < 0`，利用 C 语言无符号减法回绕特性生成正确的有符号差：

```
(int32_t)(0xFFFFFFF5 - 0x00000054) = (int32_t)0xFFFFFFA1 = -95 < 0 → ✅ 正确等待
```

**测试验证**：该测试 1/1 ✅ PASS。

### 3.3 📌 设计决策：`mk_set_task_ready()` 编译期防护

**发现途径**：代码审查

**问题**：`mk_set_task_ready()` 是 Bitmap 模式专有函数，但在 Queue 模式下调用它会编译成功、运行成功、但什么都不做——静默背叛。

**修复**：在 `core.h` 和 `core.c` 中分别用 `#if (MK_CFG_SCHEDULER_MODE == MK_MODE_BITMAP)` 包裹声明和实现。Queue 模式下调用产生 **链接器错误**，把静默 bug 变成编译期失败。

### 3.4 📌 设计决策：配置常量集中到 `config.h`

**发现途径**：代码审查

**问题**：`AO_MAX`、`MK_SCHEDULER_TICK_INTERVAL_US` 等可调参数散落在 `core.h` 中，与 API 声明混在一起。开发者需要翻阅两个文件才能找到所有配置。

**修复**：将这两个宏移至 `mid_micro_kernel_config.h`，与 `MK_AO_QUEUE_CAPACITY`、`MK_TIMER_MAX` 等配置集中管理。`core.h` 通过包含链仍可获取这些宏，已有代码无需修改。

---

## 四、测试总结

| 模块 | 用例数 | PASS | FAIL | 覆盖的关键行为 |
|---|---|---|---|---|
| Queue | 5 | 5 | 0 | 入出队、满空、绕回、FIFO、空判断 |
| Timer | 5 | 5 | 0 | 单次、周期、停止、同时到期 |
| Scheduler | 5 | 5 | 0 | 注册、投递、非法ID、广播 |
| PT_DELAY | 2 | 5 | 0 | 正常延时、49.7天溢出 |
| **合计** | **17** | **17** | **0** | |

### 4.1 质量结论

- **功能正确性**：✅ 核心事件链路完整，定时器行为正确，溢出修复通过验证
- **并发安全性**：✅ enqueue 路径有 PRIMASK 保护，位图 RMW 已修复（代码审查确认）
- **编译期防护**：✅ Queue 模式下调用 Bitmap 函数会链接失败，而非静默无操作
- **配置管理**：✅ 所有可调参数集中到 `config.h`

### 4.2 剩余风险（测试未覆盖但已知）

| 风险 | 原因 | 缓解措施 |
|---|---|---|
| 位图竞态无法通过单线程测试复现 | 需要两个线程/ISR 并发对 `s_ready_tasks_map` 做 RMW | 代码审查 + PRIMASK 保护已加 |
| 广播事件遍历 `s_ao_table` 时，另一线程在 `mk_register_ao` | 生产环境 `mk_register_ao` 在初始化阶段调用，运行时无人修改 | 文档约定：`mk_register_ao` 只在 init 阶段调用 |
| 定时器回调在 ISR 上下文中执行（`mk_timer_tick` → callback） | callback 执行时间不可控，可能影响中断延迟 | `s_post_cb` 只做 enqueue，不做事；用户不应在回调中执行长时间操作 |
