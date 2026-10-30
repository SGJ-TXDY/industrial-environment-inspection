# STM32N647 图像采集与产品检测 CAN 节点

基于 STM32N647、IMX335 摄像头和板载 LCD，完成图像采集预览、产品判定状态机、统计显示与 CAN 结果上报，与相邻 `sensor_dashboard` 网关使用协议 v2 通信。

## 已实现功能

- DCMIPP 双管线连续采集、三缓冲识别输入、LTDC 显示与 DMA2D 图像处理。
- 根据空场、正常、残次品概率进行逐件判定，残次品锁定、低置信度保守处理和 REVIEW 统计。
- 经典 CAN 500 kbit/s，FDCAN3 结果和心跳发送、RNG 会话编号、128 件 RAM 缓存及断线恢复后按序补发。
- LCD 计数、串口诊断、摄像头和推理超时故障处理。
- Simulation / SimulationFlash 配置使用可重复的模拟概率运行状态机和 CAN 联调。

## 工程与构建

工程位置：

```text
SoftwarePackage/Projects/99_Applications/996_AI_Product_Inspection/STM32CubeIDE
```

STM32CubeIDE 使用 `File → Import → Existing Projects into Workspace` 递归导入应用与 FSBL，保留本目录结构。

| 配置 | 用途 |
|---|---|
| `Simulation` | 内部 SRAM 调试、模拟分类与 CAN 联调 |
| `SimulationFlash` | 外部 Flash XIP 启动、模拟分类与 CAN 联调 |
| FSBL `Release` | 外部 Flash 启动引导 |

命令行构建：

```sh
python tools/build_firmware.py --cc /path/to/arm-none-eabi-gcc --config Simulation
python tools/build_firmware.py --cc /path/to/arm-none-eabi-gcc --config SimulationFlash
```

构建工具读取 CubeIDE 工程中的源文件、包含路径、宏和链接脚本，输出位于 `build/Appli/<配置>` 与 `build/FSBL/Release`。

## CAN 接线与协议

ATK-DNN647 V1.0 板载 SIT1050 收发器：

- FDCAN3_TX 为 PE11，FDCAN3_RX 为 PE12，AF6。
- P10 短接 1–3、2–4，连接板载收发器与 MCU。
- 与 i.MX6ULL CANH 对 CANH、CANL 对 CANL，并共地，两端各 120 Ω。
- 标准数据帧、DLC 8、500 kbit/s；结果 ID `0x210`，心跳 ID `0x211`。

每次启动产生 1–255 的随机会话号，同次运行保持不变；每件产品序号递增。结果按序发送，缓存满时丢弃新结果并累计 LOST。缓存位于 RAM，掉电或复位后清空。

详细字段见 [CAN_PROTOCOL.md](docs/CAN_PROTOCOL.md)。

## 产品判定与联调

参数位于 `inspection_config.h`：

- `p_empty ≤ 0.50` 开始或维持产品，`p_empty ≥ 0.80` 认定可信空帧。
- `p_normal ≥ 0.80` 记录正常证据，`p_defect ≥ 0.80` 锁定残次品。
- 连续有效空帧采集跨度严格大于 50 ms 时结束本件并上报一次。
- 已锁定残次品则上报残次品；否则有正常证据则正常；其余按残次品上报，并计入 REVIEW。
- 摄像头 250 ms 无帧或模型单次执行超过 100 ms 时锁存故障。

模拟配置每秒一件，700 ms 有产品、300 ms 空场。每十件中八件正常、一件高置信度残次品、一件低置信度复核。先启动网关，再启动 STM32；界面保留 `SIMULATION` 标识。

## Flash 启动镜像

生成启动镜像需要 STM32CubeProgrammer 签名工具与 Arm objcopy：

```sh
python tools/prepare_flash_images.py --config SimulationFlash --signer /path/to/STM32_SigningTool_CLI --objcopy /path/to/arm-none-eabi-objcopy
```

输出为 `build/flash/SimulationFlash/fsbl.hex` 与 `appli.hex`。CubeProgrammer 使用随附的 `tools/MX25UM25645G_ATK-CNN647B_ExtMemLoader.stldr`。FSBL 容器地址 `0x70000000`，XIP 应用代码地址 `0x70100400`；HEX 已包含地址。SRAM 配置用于调试，Flash 启动使用 `SimulationFlash`。

## 主机测试

```sh
python tools/run_host_tests.py --cc /path/to/gcc
```

覆盖产品状态机、i.MX6ULL 解析兼容性、模拟时序、CAN HAL 故障注入与摄像头缓冲管理。报告输出到 `reports/`。第三方驱动和中间件保留原始版权及许可。
