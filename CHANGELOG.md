# Changelog

## [2.0.0] — 2026-05-28

### Added
- Per-AO 独立事件队列（`mk_queue_t`），事件隔离
- 多生产者安全 enqueue（PRIMASK 临界区）
- 软件定时器模块（`mid_micro_kernel_timer.h/.c`）
- `MK_PT_DELAY(ao, ms)` 阻塞延时宏
- `mk_post_event()` / `mk_post_broadcast()` / `mk_get_tick()` API
- 交叉编译器硬件抽象宏（`MK_WFI` / `MK_DISABLE_IRQ` / `MK_DATA_MB`）
- WFI 低功耗空闲（原为注释掉的占位符）
- `MK_AO_QUEUE_CAPACITY` / `MK_TIMER_MAX` 可调配置

### Changed
- Queue 模式调度策略：全局 FIFO → Per-AO Round-Robin
- `mk_queue_init/enqueue/dequeue` API 改为 `mk_queue_t*` 参数
- `mk_active_object_t` 增加 queue / queue_storage / delay_deadline 字段
- 所有可调配置集中到 `mid_micro_kernel_config.h`

### Fixed
- `s_ready_tasks_map` 位图 RMW 竞态（Bitmap 模式下丢事件）
- `MK_PT_DELAY` uint32_t 溢出回绕（49.7 天边界误判超时）
- `mk_set_task_ready()` 在 Queue 模式下静默无操作 → 编译期链接错误

## [1.2.0] — 2026-05-28

### Added
- 遥测 / 故障回调接口（`mk_fault_cb_t`、`mk_set_timeout_threshold`）
- `mk_set_tick_callback()` 系统 tick 回调注册

## [1.0.0] — 2026-02-04

### Added
- 初始发布
- 全局单一 SPSC 锁队列
- 双模调度（Queue FIFO / Bitmap CLZ）
- Protothread 协程引擎
- DMA/中断安全（volatile 索引）
