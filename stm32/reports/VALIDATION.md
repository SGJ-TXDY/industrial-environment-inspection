# 本次验证

- 主机状态机和当前 i.MX6ULL v2 解析器测试通过，含 600 件虚拟回放。
- CAN 生产/模拟模式 HAL 故障注入测试通过，含随机会话失败、无 ACK 超时、bus-off、FIFO 满、128 条溢出、顺序补发、时钟回绕和状态故障。
- 摄像头三缓冲测试通过。
- 从最终 work/stm32 目录完成 Simulation、SimulationFlash、Production、FSBL Release 全量编译和链接。
- 新改动的 inspection_core.c、inspection_can.c、app.c 未出现编译警告；厂商驱动和中间件保留原有未使用参数等警告。
- tests 中解析器的 SHA256 与当前相邻 sensor_dashboard/app 一致；源码清单见 source_manifest_sha256.json。
- 未连接硬件，不将主机替身测试等同于真实 CAN/RNG/摄像头/LCD 验收；真实模型和签名启动容器仍未提供。
