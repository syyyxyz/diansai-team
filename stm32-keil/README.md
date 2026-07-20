# STM32F407 FPGA 接收环回验证

该 Keil 工程用于验证 STM32 是否真正收到 FPGA 返回的 AD/IQ 测量数据。当前测试条件固定为：

- DA 输出：1 kHz、约 1.0 Vpp
- DA 输出直接回接 AD 输入，即待测网络按 `H(s)=1` 处理
- 每次等待 100 ms 后累计 32 个完整周期
- SPI mode 0、MSB first、约 1 MHz
- 每 500 ms 重新测量并刷新 OLED

## 接线

STM32 与 FPGA 的 SPI1 接线：

| STM32F407 | 方向 | FPGA 顶层端口 | FPGA 管脚 |
|---|---:|---|---|
| PA4 | 输出 | `spi_cs_n` | A7 |
| PA5 | 输出 | `spi_sck` | A6 |
| PA6 | 输入 | `spi_miso` | A5 |
| PA7 | 输出 | `spi_mosi` | B7 |
| GND | - | GND | - |

原来的 DDS 控制只需要 CS/SCK/MOSI。此次回读必须额外连接 **PA6 与 FPGA MISO**，否则 OLED 会显示 `FRAME HEADER`。

OLED 沿用 `FFt` 工程的 SSD1306 I2C 接法：PB6=SCL、PB7=SDA，7 位地址为 `0x3C`。驱动已按本工程 16 MHz HSI/APB1 时钟重新计算为 100 kHz，不能直接照搬 42 MHz 参数。

## OLED 结果含义

只有出现 `RX:OK CRC:OK`，才表示 33 字节 FPGA 结果帧的帧头、响应命令、CRC-8 和测量序号全部通过校验。

- `I` / `Q`：FPGA 返回的 64 位同步检波累加值
- `N`：实际累计的 AD 样本数
- `ADC`：测量期间的最小/最大 8 位 AD 码
- `R`：`abs(Q) / abs(I)` 的百分比，最大显示 999%
- `S:BDVCOP`：依次代表 busy、done、valid、clip、overflow、protocol error；未置位显示 `-`
- `H1:PASS`：结果有效、无削顶/溢出/协议错误、AD 有动态范围且 `R <= 20%`

`H1:PASS` 只用于这一步的通信和粗略相位环回检查，不代表幅频增益已经标定。DA、AD 模块自身增益和固定延迟需在后续直通校准中消除。

如果 OLED 未连接，也可在 Keil Watch 窗口查看：

- `g_fpga_last_error`
- `g_fpga_last_status`
- `g_fpga_last_result`
- `g_fpga_rx_ok_count`
- `g_fpga_rx_error_count`

## 编译与下载

用 Keil MDK 打开 `MDK-ARM/STM32F407_FPGA_DDS.uvprojx`，选择 `STM32F407_FPGA_DDS` target 后 Rebuild，再用 ST-Link 下载。工程使用 ARM Compiler 5/C99，芯片配置为 STM32F407VETx。

核心源码：

- `Src/main.c`：固定频率环回测试、轮询及 OLED 页面
- `Src/fpga_dds.c`：SPI 命令、双事务回读、CRC 和大端字段解析
- `Src/oled_ssd1306.c`：PB6/PB7 SSD1306 驱动与紧凑 ASCII 字库
