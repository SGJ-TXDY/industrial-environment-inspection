# i.MX6ULL 环境与产品检测监控网关

面向正点原子 i.MX6ULL Mini V2.2、Linux 4.1.15 和 800×480 RGB LCD，使用 LVGL 8.4 中文触摸界面显示环境数据与 STM32 CAN 检测结果。

## 功能

- LM75BDP I²C 温度采集，ADXL345 SPI 三轴加速度与倾角计算，ATGM336H UART GPS 与 NMEA 解析。
- GT9147 I²C 电容触摸、framebuffer 显示、evdev 触摸输入及坐标翻转配置。
- CAN 结果接收、心跳监测、会话与序号去重、丢失和重发统计。
- 线程邮箱、温度回差报警、残次品声光报警、SQLite 数据记录和保留周期管理。
- MQTT 传感器、检测结果、报警与在线状态发布，支持认证、QoS 0/1、重连与内存队列补发。

## 接线

P4 为底板扩展排针，编号为物理针脚号。信号使用 3.3V 电平，按板上 1 脚标记核对方向。

| 外设 | 信号 | P4 针脚 |
|---|---|---:|
| LM75BDP | SDA / I²C2 SDA | 44 |
| LM75BDP | SCL / I²C2 SCL | 45 |
| ADXL345 | CS | 17 |
| ADXL345 | SCLK | 18 |
| ADXL345 | MISO | 19 |
| ADXL345 | MOSI | 20 |
| GPS | 模块 TX → UART4 RX | 42 |
| GPS | 模块 RX ← UART4 TX | 43 |
| 电源 | GND | 47 |
| 电源 | 3.3V | 48 |

LM75BDP A0/A1/A2 接地，地址为 `0x48`，与地址 `0x14` 的 GT9147 共用 I²C2。LCD 和触摸使用原排线。GPS 按模块电源标识供电。

CAN 使用板载 TJA1050 对应的 CAN1 端子，与 STM32 收发器 CANH、CANL 对接并共地，两端各 120 Ω。LED0 与蜂鸣器由 `env_alarm` 驱动控制，均低电平有效。

## 构建部署

依赖 Linux 构建环境、`arm-linux-gnueabihf-` 工具链及板卡配套内核源码。内核目录需已有板级 `.config`。

```sh
make -j4 KDIR=/path/to/linux-kernel
make dtbs KDIR=/path/to/linux-kernel
make install KDIR=/path/to/linux-kernel DESTDIR=/path/to/rootfs TFTP_DIR=/path/to/tftpboot
```

默认路径见 `Makefile`。构建包含应用、SQLite 工具、LVGL、Paho、内核和四个外部模块；设备树编译及安装脚本包含原文件备份。部署后让 U-Boot 加载对应 `zImage`、`imx6ull-alientek-emmc.dtb` 和根文件系统。

```sh
/etc/init.d/S99sensor-dashboard start
/etc/init.d/S99sensor-dashboard stop
/etc/init.d/S99sensor-dashboard restart
cat /var/log/sensor-dashboard.log
```

## 配置

完整默认配置见 [scripts/sensor-dashboard.conf](scripts/sensor-dashboard.conf)，板端路径为 `/etc/default/sensor-dashboard`。

| 参数 | 默认值 / 作用 |
|---|---|
| `GPS_DEVICE` / `GPS_BAUD` | `/dev/ttymxc3` / `9600` |
| `FB_DEVICE` | `/dev/fb0` |
| `TOUCH_DEVICE` | 留空时按 `env-gt9147` 自动查找 |
| `TOUCH_SWAP_XY` / `TOUCH_INVERT_X` / `TOUCH_INVERT_Y` | 触摸方向调整，默认均为 0 |
| `CAN_DEVICE` / `CAN_BITRATE` | `can0` / `500000` |
| `ALARM_TEMP_LOW` / `ALARM_TEMP_HIGH` | 0 °C / 40 °C |
| `ALARM_HYSTERESIS` | 1 °C |
| `ALARM_DEFECT_MS` | 残次品报警 1000 ms |
| `DB_PATH` | `/var/lib/sensor-dashboard/sensor.db` |
| `DB_SENSOR_PERIOD` / `DB_KEEP_DAYS` | 10 秒 / 30 天 |
| `MQTT_HOST` / `MQTT_PORT` | 服务器地址 / 1883 |
| `MQTT_USERNAME` / `MQTT_PASSWORD` | 服务器认证信息 |
| `MQTT_TOPIC_PREFIX` | `imx6ull/sensor-dashboard` |
| `MQTT_QOS` / `MQTT_BACKLOG` | 1 / 200 条 |

设置 MQTT 服务器后，向主题前缀下的 `/sensor`、`/inspection`、`/alarm` 和 `/status` 发布消息。在线状态使用保留消息，异常断线由遗嘱消息更新状态。队列满时丢弃最旧消息。

应用通过 rtnetlink 设置 CAN 波特率并启用接口，内核 bus-off 自动恢复时间为 100 ms；`CAN_BITRATE=0` 时保留外部设置的速率。

## 目录与测试

- `app/`：界面、采集、CAN、线程邮箱、数据库、报警与上传。
- `drivers/`、`include/`：外部驱动与共享数据格式。
- `scripts/`：构建、部署、启动和配置。
- `third_party/`：LVGL、SQLite 和 Paho 源码及许可。
- `tests/`：逻辑、线程、数据库、MQTT、UI 和安装测试。
- `docs/`：协议、预览图、原理图针脚参考与技术记录。

```sh
make test
sh tests/test-install.sh
```

协议见 [CAN_PROTOCOL.md](docs/CAN_PROTOCOL.md)，来源见 [SOURCES.md](docs/SOURCES.md)。
