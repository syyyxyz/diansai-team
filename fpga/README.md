# FPGA AD 同步检波工程

本工程在原有 48 位 DDS/AD9708 输出基础上，加入 AD9280 采集、同步 I/Q 检波、完整周期测量和 SPI 回读。顶层为 `dds.vhd`，器件为 `EP4CE10F17C8`，核心时钟 125 MHz，AD 时钟输出 25 MHz。

## 数据通路

```text
AD9280 offset-binary data
        |
        v
adc_capture -- 9-bit signed sample, min/max/OTR
        |
        +---- DDS phase sampled every AD clock ---- adc_phase_ref
        |                                               |
        +------------------- iq_demod <-----------------+
                                  |
                         I/Q int64 + N + status
                                  |
                           spi_measure_if
                                  |
                                STM32
```

DA 和 AD 来自同一 125 MHz 时钟域。AD 每五个核心时钟采样一次，因此在 AD 采样点读取 DDS 相位，等价于以 `5 * FTW` 作为 25 MHz 检波相位步进。模拟链路、DA 流水和 ADC 转换造成的固定相移由直通复数校准消除。

测量流程为：等待暂态稳定、等待 DDS 相位回绕、清累加器、累加指定完整周期、排空乘法流水、锁存结果、置 `valid/done`。测量期间收到新的 `SET_SINE` 会被拒绝。

## SPI 协议

SPI mode 0，MSB first，当前接口按约 1 MHz SCK 设计。CRC 为 CRC-8/ATM，多项式 `0x07`，初值 `0x00`，覆盖 CRC 字节之前的全部字节。所有多字节字段均为大端。

请求帧：

| 命令 | 请求内容（不含说明文字） |
|---|---|
| `SET_SINE 0x01` | `A5 01 FTW[6] AMP_Q16[2] CRC` |
| `START 0x02` | `A5 02 SEQ CYCLES[2] SETTLE_MS[2] CRC` |
| `READ_STATUS 0x03` | `A5 03 CRC` |
| `READ_RESULT 0x04` | `A5 04 CRC` |
| `CLEAR_RESULT 0x05` | `A5 05 CRC` |

读操作采用两个 SPI 事务：先发送 `READ_*` 请求并释放 CS，再在下一个事务发送任意字节以从 MISO 移出响应。

- 状态响应（5 字节）：`A5 83 SEQ STATUS CRC`
- 结果响应（33 字节）：`A5 84 SEQ FTW[6] I[8] Q[8] N[4] MIN MAX STATUS CRC`

`I`、`Q` 是 64 位二补码有符号数。`STATUS` 位定义：bit0 `busy`、bit1 `done`、bit2 `valid`、bit3 `clip`、bit4 `overflow`、bit5 `protocol_error`，bit7:6 保留。

`AMP_Q16=0` 静音，`65535` 为原始 ROM 满幅；`CYCLES=0` 时 FPGA 使用 100 周期。结果中的频率字段是 48 位 DDS FTW，不是 Hz：

```text
frequency_hz = FTW * 125000000 / 2^48
```

## 引脚约束

DA 引脚沿用原始 `dds.qsf`，AD 引脚取自队友提供的 `dds(1).qsf` 参考工程：`ad_clk_out=PIN_T12`，`ad_data[7:0]=PIN_T13,PIN_R13,PIN_T14,PIN_T15,PIN_R16,PIN_P16,PIN_N15,PIN_N16`。参考工程未使用 AD9280 的 OTR，因此本工程也不把 OTR 作为顶层端口，内部过量程输入固定为 0；削顶仍可通过 ADC 码值接近 `0/255` 检测。

STM32 MISO 暂定连接 FPGA `PIN_A5`；若实际导线不同，修改该项约束即可。

## 编译和仿真

```sh
cd /home/iris/Diansai/fpga
ghdl -a --std=08 adc_capture.vhd adc_phase_ref.vhd iq_demod.vhd measure_ctrl.vhd spi_measure_if.vhd sim/tb_iq_demod.vhd sim/tb_measure_ctrl.vhd sim/tb_spi_measure_if.vhd
ghdl -e --std=08 tb_iq_demod
ghdl -r --std=08 tb_iq_demod --assert-level=error
ghdl -e --std=08 tb_measure_ctrl
ghdl -r --std=08 tb_measure_ctrl --assert-level=error
ghdl -e --std=08 tb_spi_measure_if
ghdl -r --std=08 tb_spi_measure_if --assert-level=error --stop-time=2ms
quartus_sh --flow compile dds
```

当前完整 Quartus 编译为 0 error，125 MHz 最差 setup slack 为 `+0.695 ns`、最差 hold slack 为 `+0.186 ns`。AD 物理引脚已固定；Timing Analyzer 的未完全约束提示来自尚未按实测板级延迟建立 AD 数据输入时序窗口，不影响本阶段生成 SOF 和低频联调。

## 首次硬件验证

1. AD 模拟输入接 0 V，确认回读 `MIN/MAX` 位于约 128 附近。
2. DA 直连 AD，在 1 kHz、低幅度下测量，确认 `N>0`、无 clip，I/Q 多次测量稳定。
3. 改变 DA 幅度，确认 `sqrt(I^2+Q^2)/N` 近似成比例。
4. 完成 100 Hz～3 kHz 直通校准后，再接未知电路；STM32 对未知结果和直通结果做复数相除。
