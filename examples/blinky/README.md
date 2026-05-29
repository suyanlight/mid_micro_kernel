# Blinky 示例

演示 Micro-Kernel v2.0 最简用法：两个 Active Object（LED、UART）通过软件定时器驱动，实现 500ms 闪烁 + 1s 打印。

## 将此示例应用到你的 MCU

需要替换以下硬件相关的实现：

| 符号 | 替换为 |
|---|---|
| `LED_PORT` / `LED_PIN` | 你板子上的 LED GPIO |
| `gpio_toggle()` | 你的 GPIO 翻转宏 |
| `uart_puts()` | 你的串口输出函数 |
| `get_sys_tick_ms()` | 你的 SysTick 计数值读取 |
| `SysTick_Handler` | 你的 SysTick 中断入口 |

## 文件清单

- `main.c` — 全部代码（不含平台初始化代码）

## 编译说明

将 `main.c` 与 `src/` 下的全部 .h/.c 加入你的 MCU 工程，配置 SysTick 为 1ms 即可。
