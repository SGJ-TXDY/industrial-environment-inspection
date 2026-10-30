# 接入真实产品质检模型

当前没有真实残次品模型；不得修改文字标签后复用火焰/烟雾模型。

## 接口合同

- 基础适配器要求单输入、UINT8 RGB、NHWC，默认 1×160×160×3；DCMIPP 输出已按模型像素顺序配置。
- 输出要求 FLOAT32[3]。网络编译时让运行库完成输出反量化；当前适配器不直接读取 INT8 输出。
- 三个语义为 EMPTY、NORMAL、DEFECT，指固定检测区域内的无产品、正常产品、残次品。
- 训练/验证需包含真实光照、正常输送带空背景、产品进入/离开过程；低分辨率能否看清缺陷应通过验证集确认。
- 如模型使用不同输入归一化、BGR、INT8 输入、多个输出或不同尺寸，先修改并验证适配器；不能只打开可用宏。

## 接入步骤

1. 用当前 STM32N6 工具链将模型转换为 Neural-ART 网络，保留完整生成报告、生成参数和类别映射。
2. 将生成的 network.c、network_ecblobs.h 和其他配套源/头文件放入应用的 Model/Generated；该目录已加入 CubeIDE 链接资源，独立构建脚本也会枚举其中 C 文件。
3. 当前适配代码使用 Default 网络实例及 LL_ATON_Input_Buffers_Info_Default / Output_Buffers_Info_Default。生成实例名不同时统一修改适配代码。
4. 设置 INSPECTION_MODEL_AVAILABLE=1。Production 的 INSPECTION_SIMULATION 保持 0。可在 inspection_config.h 修改默认值或增加编译宏。
5. 核对 INSPECTION_INPUT_WIDTH/HEIGHT；默认 RGB888。缓冲 stride 会按 16 字节对齐，适配器逐行复制为模型连续输入。
6. 默认模型输出是 logits，适配器只执行一次稳定 Softmax。模型已输出概率时，设置 INSPECTION_OUTPUT_IS_LOGITS=0。
7. 设置 INSPECTION_MODEL_EMPTY_INDEX、INSPECTION_MODEL_NORMAL_INDEX、INSPECTION_MODEL_DEFECT_INDEX，三者必须是 0、1、2 的唯一排列。
8. 将权重按生成报告部署到实际外部 Flash 地址。核对生成的激活区不覆盖链接脚本 RAM、摄像头和 LCD 缓冲；不能直接沿用别的模型报告。
9. 先用标注的固定图像比较 PC 与板端的三类概率，再接摄像头；检查输入像素顺序、量化参数、反量化和 Softmax。
10. 最后执行 ACCEPTANCE 中的真实性能与流水线验收。

## 分步运行与超时

模型适配器复制输入并清理 CPU DCache，调用 RuntimeInit/Init_Network；主循环每次调用 RunEpochBlock，完成后同步输出缓存并取概率，再释放运行库状态。

100 ms 是模型运行超时。模型故障锁存后，不启动下一个网络；需要重启以恢复。软件回退层本身如果长时间阻塞，主循环无法抢占该层，因此选用的模型应主要在 NPU 运行，并通过实际最长帧间隔验收。

未提供模型时，生产构建保留摄像头和故障心跳，序号保持 0，不生成虚假合格产品结果。
