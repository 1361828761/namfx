# TODOS

推迟工作收集。每个条目的 Context 供 3 个月后的接手者理解动机。
当前里程碑见 `docs/EXECUTION.md` §9（M0 执行清单）。

## P2 — 后续迭代

- [ ] **练习辅助（节拍器/鼓机/循环器）**
  - What: 节拍器（click 发生器）+ 鼓机（采样播放/pattern）+ 循环器（looper）
  - Why: grill-me G2；CEO 评审指出竞品练习场景都做得弱，这是差异化机会
  - Pros: 练习场景刚需；差异化
  - Cons: 引擎需要采样播放/pattern 逻辑，第一版范围变大
  - Context: 全部推迟；调音器已纳入 M5，练习三件套留后续。Looper 是 Tonex 头号差评点，引擎 M2 预留缓冲抽象，v1.1 出货基础 60s 单轨 looper
  - Depends on: M5
  - Priority: P2

- [x] **演出模式（live mode）** ✅ 已拆分落计划（v0.6 #61 / v0.7 #83）：演出基础（预设/场景切换+旁路+调音器+锁定视图）M5c 交付；手动模式（脚踩直控链上模块）M7a 硬件脚控后做

- [ ] **预设分享/生态（社区分享格式、云同步）**
  - What: 预设导出/导入、分享格式、可能的社区/云同步
  - Why: 差异化押注点"开放格式 + 社区生态 + 价格"；护城河在内容与生态
  - Pros: 战略级差异化；用户留存
  - Cons: 需要服务端或纯文件共享方案决策
  - Context: CEO 评审 5.3：做 NAM 播放器无护城河，生态是护城河。**导出/导入基础已归 M5a（v0.7 #67），社区层仍开放**
  - Depends on: M5
  - Priority: P2

- [ ] **蓝牙音频/控制（M7b 后低成本差异化）**
  - What: 蓝牙接入（伴奏播放 / 无线控制）
  - Why: CEO 二轮 F5-2；练习场景无线伴奏入口
  - Pros: 低成本差异化
  - Cons: 音频蓝牙延迟高，仅适合伴奏；控制类需协议决策
  - Context: 二轮遗留，v0.7 #71 补录（此前在 TODOS 丢失）
  - Depends on: M7b
  - Priority: P2

- [ ] **云捕获路线图（上传干湿→返回 .nam）**
  - What: 用户上传干/湿音频对，云端训练返回 .nam 模型
  - Why: CEO 二轮 F5-5；捕获即内容是生态护城河动作
  - Pros: 生态闭环
  - Cons: 服务端训练成本高；仅单人项目则不可行
  - Context: 二轮遗留，v0.7 #71 补录（此前在 TODOS 丢失）
  - Depends on: M4
  - Priority: P2

- [ ] **WDF 电路级单块库（TS9/Klon 类）**
  - What: 白盒电路建模驱动类单块
  - Why: 用户原始诉求（DSP 复刻单块）；前提评审降级为后置
  - Pros: 全参数可调单块，不受 NAM 微调限制；不依赖社区捕获质量
  - Cons: WDF 音质难追平商业 DSP；与社区 NAM 捕获重复；商标需改名
  - Context: 前提 2 用户选择"通用模块优先，WDF 单块后置"
  - Depends on: M2 DSP 库框架
  - Priority: P2

- [ ] **模块第三方扩展接口**
  - What: 开放模块 API 供第三方开发 DSP/IR 模块
  - Why: 生态护城河；开源分发策略联动
  - Pros: 社区贡献；差异化
  - Cons: API 冻结承诺；ABI 稳定性
  - Context: 原开放问题；M8 后考虑
  - Depends on: core 稳定
  - Priority: P3

## P3 — 研究/远期

- [ ] **MCU 上 NAM 实时性研究（量化推理）**
  - What: A1/A2Lite 在 MCU 类平台实时推理的可行性研究
  - Why: 打开"低档平台跑 NAM"的可能；用户双端兼容诉求
  - Pros: 双端承诺完整化
  - Cons: 业界基本无解；大概率维持 DSP+IR 结论
  - Context: 前提 3 用户确认 NAM 加载层兼容；M8 为研究性里程碑
  - Depends on: M4 NAM 集成
  - Priority: P3

- [ ] **中文市场定位页**
  - What: 市场假设一节（用户/场景/价格/渠道）
  - Why: CEO 评审 5.4：中文市场机会被忽略
  - Pros: 方向校准
  - Cons: 个人/学习项目可能不需要正式市场文档
  - Context: 差异化押注需验证
  - Depends on: 无
  - Priority: P3

## P1 — 近期（M0/M1 范围内）

- [ ] **moddsp/modep 生态调研（M0，半天）**
  - What: 调研 MOD Devices 开源平台（嵌入式+桌面共享 LV2 踏板链）
  - Why: CEO 评审 4.2：可能与本项目目标高度重合，确认是否有直接省 3 个里程碑的路径
  - Pros: 避免重复造轮子；可借鉴架构
  - Cons: 半天时间成本
  - Context: 评审确定加入 M0
  - Depends on: M0
  - Priority: P1

- [ ] **嵌入式硬件采购与实测（M0 起）**
  - What: 采购 STM32H7 核心板 + I2S codec + **图形点阵 LCD（含中文字库，中文全端显示 v0.7 #82）**，实测默认音频栈延迟（去 STL/单精度适配 + DSP+IR 实时性 + A2 Lite 档探针）
  - Why: CEO 评审 2.1/3.4：5ms 数字泡沫风险；用数据决定预算；用户指定 H7 为起步板（省成本）
  - Pros: 延迟目标基于实测而非猜测；验证引擎可移植性
  - Cons: 硬件成本 ~¥200
  - Context: 多芯片 DSP 方案已否决（引擎复用 0% + NAM 无法运行）；量产档位 M7 用 H7 vs 国产 Cortex-A 实测对比决定
  - Depends on: M0
  - Priority: P1

- [ ] **国产 Cortex-A 核心板评估（M4 前）**
  - What: 评估 RK3308/RK3328 级核心板（性能/成本/功耗/BSP 生态），为量产档位备选
  - Why: H7 算力边界内 NAM 大概率不可行；主力档需要 Cortex-A 兜底
  - Pros: 量产形态明确
  - Cons: 国产 SoC 文档生态弱，BSP 适配需缓冲
  - Context: M7 双档位实测对比后定量产；**v0.7 #71 修正：依赖 M7→M4（M4 A2 bake-off 需要板在手，不等 M7）**
  - Depends on: M4
  - Priority: P2

- [ ] **苹果开发者账号（M5 前）**
  - What: 提前办理 macOS 开发者账号 + 公证证书
  - Why: 评审 3.4：公证/签名是金钱与时间成本，AU 验证流程烦人
  - Pros: M6 发布不卡壳
  - Cons: 年费
  - Context: M6 插件里程碑前置条件
  - Depends on: M6
  - Priority: P1

- [x] **NAM Core 许可清单（M4 前）** ✅ 已解决（v0.6 #55：Core/Plugin/Trainer 全 MIT，依赖栈无 RTNeural，闭源内嵌允许）
