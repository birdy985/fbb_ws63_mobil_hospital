# 16_gt_health

冠拓电子心率血氧血压传感器模块串口读取示例。

- UART: `UART_BUS_1`
- TX: `GPIO15`, `PIN_MODE_1`，对应 HH-D02 排针上标注的 `UART1_TX`
- RX: `GPIO16`, `PIN_MODE_1`，对应 HH-D02 排针上标注的 `UART1_RX`
- Baudrate: `38400`
- 串口屏已占用 `UART_BUS_2` 的 `GPIO08/GPIO07`，本示例避开这组引脚。

注意：这里的 `GPIO15/GPIO16` 是 WS63 芯片管脚编号，不是 HH-D02 排针序号 15/16。HH-D02 前 14 个排针里通常会引出 `UART1_RX/UART1_TX`，接线请按排针丝印接：

- 传感器 `TXD` 接 HH-D02 `UART1_RX`
- 传感器 `RXD` 接 HH-D02 `UART1_TX`
- 传感器 `GND` 接 HH-D02 `GND`
- 传感器电源按模块要求接 `3.3V` 或 `5V`

模块协议按资料示例处理：初始化后发送 `0x8A`，随后同步 `0xFF` 帧头并读取 88 字节数据帧。第 1-64 字节为波形，第 65 字节为心率，第 66 字节为血氧，第 71/72 字节为收缩压/舒张压。
