# 架构说明

## 设计哲学

事件驱动的协作式主动对象（Active Object）架构。每个 AO 有自己的事件队列和 protothread 协程，调度器按策略（轮询 / 优先级）驱动所有 AO。

```
┌──────────────────────────────────────────────────┐
│                   SysTick ISR                     │
│         mk_timer_tick() → 到期 → mk_post_event()  │
└────────────┬─────────────┬──────────────┬─────────┘
             │             │              │
        事件入队       事件入队        事件入队
             ▼             ▼              ▼
┌──────────────────────────────────────────────────┐
│              Per-AO 队列阵列                      │
│   ┌──────┐  ┌──────┐  ┌──────┐      ┌──────┐    │
│   │AO#0  │  │AO#1  │  │AO#2  │ ...  │AO#15 │    │
│   │queue │  │queue │  │queue │      │queue │    │
│   └──┬───┘  └──┬───┘  └──┬───┘      └──┬───┘    │
└──────┼─────────┼─────────┼─────────────┼────────┘
       │         │         │             │
       ▼         ▼         ▼             ▼
┌──────────────────────────────────────────────────┐
│               Scheduler Main Loop                 │
│                                                   │
│  Queue Mode:   for(i=0; i<AO_MAX; i++) {         │
│                   dequeue → pf_dispatch();        │
│                }                                  │
│                                                   │
│  Bitmap Mode:  CLZ(s_ready_tasks_map)             │
│                   → dequeue → pf_dispatch();      │
│                                                   │
│  空闲时 → WFI()                                   │
└──────────────────────────────────────────────────┘
```

## 模块依赖

```
config.h  ←── type.h  ←── queue.h
    ↑                      ↑
    ├── pt.h               │
    │                      │
timer.h ←─── core.h ───────┘
                │
           (用户应用代码)
```

- `config.h`：无依赖，所有模块都引用它
- `type.h`：依赖 pt.h + config.h
- `queue.h`：依赖 type.h（用 mk_event_msg_t）
- `core.h`：依赖 type.h（用 mk_active_object_t）
- `timer.h`：依赖 config.h（用 MK_TIMER_MAX）

## 调度模式选择

| 模式 | 优点 | 缺点 | 适用场景 |
|---|---|---|---|
| Queue (默认) | 事件携带数据 payload，FIFO 顺序可预期 | Round-Robin 扫全表，AO 越多单次循环越长 | 通用场景、传感器采集、命令解析 |
| Bitmap | CLZ 单指令取最高优先级 | 事件不携带数据（AO 自己读共享变量） | 优先级明确的简单信号系统 |

通过 `mid_micro_kernel_config.h` 中的 `MK_CFG_SCHEDULER_MODE` 切换。

## 内存布局

```
.bss / .data (静态分配，无 malloc)
├── s_ao_table[16]              ← AO 指针表 (64B)
├── s_ready_tasks_map            ← 就绪位图 (4B)
├── s_timers[16]                 ← 软件定时器池 (192B)
├── 每个 AO:
│   ├── mk_active_object_t       ← ~32B
│   └── queue_storage[N×8]       ← N×8B (N = MK_AO_QUEUE_CAPACITY)
└── 函数指针 (pf_dispatch) 在 .text / ROM
```

## ISR 安全模型

```
ISR context:
  mk_post_event()
    └─ mk_queue_enqueue()
         ├─ MK_DISABLE_IRQ()
         ├─ buffer[wr_idx] = *msg    ← 拷贝事件
         ├─ wmb (DMB)
         ├─ wr_idx++                 ← 使消费者可见
         └─ MK_ENABLE_IRQ()
```

关键决策：enqueue 路径加 PRIMASK 临界区，dequeue 路径不加（单消费者，运行在 thread 优先级，不会被调度器自身打断）。
