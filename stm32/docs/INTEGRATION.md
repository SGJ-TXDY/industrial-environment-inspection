# STM32 与 i.MX6ULL 集成交付说明

## 使用入口

将本 stm32 目录与 sensor_dashboard 并列放在 work 中。导入 SoftwarePackage/Projects/99_Applications/996_AI_Product_Inspection/STM32CubeIDE，包含 Appli 和 FSBL。

- 双板演示：SimulationFlash 应用和 Release FSBL；SRAM 调试可用 Simulation。
- 真实质检：Production，仍需 MODEL_INTEGRATION.md 要求的模型和权重。
- 不要将原始 FSBL BIN 直接当作 Flash 启动镜像；先使用 prepare_flash_images.py 和 CubeProgrammer 签名工具生成容器。

## 修改内容

1. 默认发送 CAN v2：结果 byte 3、心跳 byte 2 携带一致的非零启动会话。
2. HAL RNG 产生会话；失败显式累计并重试，不以启动 tick 冒充随机数。CAN 恢复保留会话。8 位随机会话仍存在 1/255 相邻碰撞，未增加 Flash 擦写或占用备份寄存器。
3. 心跳 byte 3 bit 0 表示模拟。当前 sensor_dashboard 接收器兼容但忽略该标记；并未声称接收端已完成模式显示或存储隔离。
4. 128 件 RAM 队列，单件在途，确认硬件完成后出队；超时、bus-off、HAL 状态失败保留队首并恢复重发。FIFO 满时等待，不阻塞视觉主循环。队满丢最新，已保留的结果顺序不变。
5. LCD 显示 SID/Q/LOST，串口增加 session/backlog/lost/retry/rng_fail。TX DONE 是硬件完成数，FAIL 是尝试失败数，LOST 是队满丢弃的件数；三者不同。
6. 同步实际 i.MX6ULL v2 接收器副本到 tests，新增重启同序号、模拟模式、RNG、重试、缓存容量、时钟回绕和 HAL 故障测试。

## 自动验证

运行 tools/run_host_tests.py（可用 --cc 指定本机 GCC）：状态机/协议回放、生产 CAN 后端、模拟 CAN 后端、摄像头三缓冲测试。报告在 reports。测试使用真实固件 C 文件，HAL 用可控替身注入故障，不连接硬件。

运行 tools/build_firmware.py --cc <arm-none-eabi-gcc路径> --config All：Simulation、SimulationFlash、Production 和 FSBL/Release。工具链不捆绑进源码，迁移机器后需要自行安装或使用 CubeIDE。

reports/firmware_builds.json 和 build 下的日志包含编译结果。Production 编译通过仅代表缺模型故障路径可构建，不代表已有真实 AI 模型。验收步骤见 ACCEPTANCE.md。

## 限制

- 队列容量有限且不掉电保存；没有应用层确认，CAN ACK 不代表接收进程读取或数据库提交成功。
- 模拟标记尚需接收端后续支持；演示应使用单独数据库和 MQTT 主题。
- 延迟补发时，接收端时间为到达时间；结果帧尚无采集时间戳。
- 不修改 i.MX6ULL 驱动、数据库或 UI。真实 CAN、RNG、摄像头、LCD、FSBL 启动和模型速度需要上板验收。
- 尚未提供真实模型，也未生成签名启动容器或执行烧录。
