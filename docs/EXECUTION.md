# EXECUTION — 执行手册（新接手者唯一入口）

> 这是本项目的**执行文档**。无论你是新工程师还是新 AI 模型，**从这里开始**。
> 读完本文件即可开始执行当前任务；需要"为什么"时查阅 `docs/PLAN.md`；术语用根目录 `CONTEXT.md`；推迟项在 `TODOS.md`。
>
> 状态：计划 **APPROVED（v0.8：#109 RK3308 改判 + #110 名单块复刻 + #111 GPL-3.0）**，当前任务 = **M2 第一批：TS808 风格过载（电路级 WDF）**，见 §9。M1 软件部分已完成；M1 硬件验收项（RK3308 交叉编译 + UAC2 spike ≥8h）待用户下单板子。

---

## 1. 项目一句话

智能电吉他综合效果器平台：PC（JUCE 独立应用 + VST3/AU）+ 嵌入式（RK 系 Cortex-A Linux 主力 / STM32H7 低端）**共享同一纯 C++17 音频引擎**，支持 NAM 模型（.nam）、IR 卷积（.wav）、DSP 算法三类音色模块，5ms 目标延迟，双端一套预设格式。

## 2. 文档地图

| 文件 | 作用 | 何时读 |
|---|---|---|
| `docs/EXECUTION.md`（本文件） | 怎么干、当前任务、红线、坑 | **开始干活前必读** |
| `docs/PLAN.md` | 全部决策 + 理由 + 111 条决策审计（#1-111） | 需要"为什么"时 |
| `CONTEXT.md` | 术语规范（22 词） | 写代码/文档前查词 |
| `TODOS.md` | 推迟的工作 | 本里程碑完成后 |

## 3. 环境与构建

| 项 | 要求 |
|---|---|
| 编译器 | MSVC 2019+（Windows）/ GCC 9+ 或 Clang（Linux/macOS） |
| CMake | ≥ 3.24（用 `CMakePresets.json`）。**本机 cmake 不在 PATH**：需在 VS 2026 Developer PowerShell 中执行，或全路径 `D:\study\vc\vsiualstudio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` |
| C++ 标准 | **C++17**（core 必须保持 17，不得升级） |
| 测试框架 | Catch2 v3（`tests/unit/` 用 FetchContent 引入，仅测试目标依赖） |
| 构建系统 | CMake + 各平台原生生成器（VS/Unix Makefiles/Ninja） |

```bash
# 配置 + 构建 + 测试（Windows PowerShell 示例）
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

CI 要求（从 M0 起）：Windows + Linux 双平台构建、跑全部单元测试；延迟断言只测引擎处理延迟（`tools/latency`），端到端延迟人工跑。

## 4. 已定决策速查（不许重新提问）

以下决策已定死。执行中**不得**因为"看起来更合理"而改动，改需回到计划评审流程。

| # | 决策 | 一句话理由 |
|---|---|---|
| D1 | 双端共享引擎：core 纯 C++17 零第三方运行时依赖；桌面/嵌入式是薄壳 | 双端同步更新 |
| D2 | 桌面 UI = JUCE；插件 = VST3 + AU；独立应用优先 | 行业标准 |
| D3 | 延迟：5ms 目标 / 8ms 验收底线；分模块预算（音高模块豁免） | 手感阈值实测 |
| D4 | 桌面音频后端：Windows WASAPI 独占为主 + ASIO 可选；macOS CoreAudio | 免驱动即插即用 |
| D5 | 音色链 ≥8 槽位可扩展；槽位数不做营销话术 | 真实需求 4-6 个 |
| D6 | 预设 = 链 + 8 场景；JSON schema v1 含 scenes 字段 + 迁移链 | 双端共享格式 |
| D7 | 场景切换 = 即时参数应用 ≤10ms；干音直通（G12）只用于预设切换 | 舞台无断点 |
| D8 | A/B = 临时对比，不持久化不入 undo；undo/redo 是**壳侧概念**（桌面壳与 Web 壳实现，引擎不背，LCD 无） | 语义清晰 |
| D9 | 控制源统一抽象（踏板/MIDI CC/Web UI/桌面 UI/脚踩）+ 多源仲裁；引擎是参数权威 | 并发不打架 |
| D10 | 模块注册表：模块ID 全局唯一键，类别（pedal/amp/cab）是属性 | 防静默失败 |
| D11 | 三技术：NAM（官方 Core，A1/A2 slimmable）/ IR（UPOLS 分区卷积）/ DSP（**名单块数字复刻**，见 D12） | 互补覆盖 |
| D12 | DSP 库 = **名单块数字复刻**（#110）：非线性音色→电路级 WDF（TS808 先行→Klon/OCD），时间/滤波特征→行为级复现（CE-2/Phase 90/DM-2/Echoplex 等）+ 音高家族（单音假设） | 用户拍板：复刻名块路线 |
| D13 | 性能分级 Full/Limited/Minimal + 运行时实测；**低档平台 slimmable A2 文件经显式确认以 Lite 档加载**（非 slimmable 才拒绝提示换 A1） | 用户裁决：音色不变幻 |
| D14 | 嵌入式：主力档 = RK3308/RK3328/RK3566 候选（M4 bake-off 定）；低端档 = STM32H7；多芯片 DSP 方案否决 | ARM 路线 + 引擎复用 |
| D15 | M0 起步板 = **RK3308 ev 板**（~¥100-150，内置 8ch ADC/2ch DAC codec；用户 2026-08-13 改判 #109，原 STM32H7 降为低端档 M8 研究） | 一板三用：UAC2 spike + bake-off + 主力验证 |
| D16 | UAC2 spike 前置 M0-M1（M1 收 spike 跑通+≥8h，**24h 结论移 M2**） | 双时钟域最深坑提前踩 |
| D17 | 固件升级 v1 = rkdeveloptool + bootcount 回滚；真 A/B 后置 v2 | 1-1.5 周 vs 3-5 周 |
| D18 | 硬件接口：高阻输入/平衡输出/耳机/效果环路(M7b)/表情踏板(M7b)/UAC2(M7c)；**无 DIN MIDI** | USB-MIDI 覆盖 90% |
| D19 | 演出基础（预设/场景切换+旁路+调音器）M5；手动模式 M7 | 桌面 MIDI 可触发 |
| D20 | 调音器 M5（全屏+指针刻度+自动静音）；全局 EQ/输出模式 M5b | 刚需 |
| D21 | 备份：自动 5 版 + 升级前全量 + 原子写入 | 防误删防变砖 |
| D22 | 演示预设：M1-M4 做 5-10 个全 DSP，M5 前扩到 30+（含中文名） | 首开能弹 |
| D23 | 嵌入式 UI：Linux = Web UI（控制源之一 + 演出锁）；H7 = LCD 两级；脚踩+编码器日常操作 | 层级/仲裁定死 |
| D24 | 嵌入式设计冻结到桌面 v1 发布后，不反向污染桌面 | 防止前置成本 |
| D25 | JSON 序列化：nlohmann/json header-only 范围豁免（音频路径零依赖红线不变，仅加载路径） | C++17 std 无 JSON |
| D26 | 参数写优先级：场景 recall > 控制源 > UI；绑定期 UI 写入=深度 1 排队；hang-time 100-300ms 回落走平滑 | 三写者不打架 |
| D27 | 现场状态记忆 = 变更后 debounce 保存（2-5s）+ 原子写协议 + CRC；断电窗口只 mute | "断电时保存"物理不可行 |
| D28 | LCD 中文全端显示：图形点阵屏 + 中文字库（BOM +¥10-20、开发 +1-2 周） | 用户裁决（#82） |
| D29 | 延迟分档：64 样本 = 5ms 目标档；128 样本 = 8ms 底线档（128 双缓冲已 5.33ms+） | 原表数学错一倍 |
| D30 | **许可证：整体 GPL-3.0**（#111 用户拍板，原 MIT）；WDF 库豁免入 dsp 建模路径 | 质量优先，纯开源路线 |

## 5. 技术事实速查（已核验，不许重新查证）

| 事实 | 结论 | 影响 |
|---|---|---|
| NAM 架构 | Core 加载层兼容 A1/A2/LSTM 全部 .nam；**A2 是 slimmable**：同一文件含 3ch(Lite)/8ch(Full) 子模型，运行时切换 | 性能档位可切 A2 档 |
| NAM 量化 | 官方无 INT8/FP16 路径，权重固定 float32 | 不要做量化探针 |
| NAM 许可 | Core/Plugin/Trainer 全 MIT；依赖 Eigen(MPL2)/nlohmann(MIT)/AudioDSPTools(MIT)，无 RTNeural | NAM MIT 与本项目 GPL-3.0（#111）兼容；"闭源内嵌"结论随 #111 作废 |
| NAM 算力 | A2-Full:Lite ≈7-15×；A2-Lite 在 A35/A53 实时性"大概率可行但未实测" | M4 必须 bake-off |
| RK3308 | 4× Cortex-A35 @1.3GHz（主线内核默认 1.008GHz）；内置 codec = **8ch ADC + 2ch DAC**；8ch I2S 回放侧仅 2ch | 输入复用内置 ADC；输出需外置 I2S DAC |
| RK3328 | 4× Cortex-A53 @1.5GHz，约 1.5-2× RK3308 算力；带 GPU/DDR4/USB3 | 性能备选 |
| UAC2 | Linux configfs 组合设备成熟；异步反馈官方支持（c_sync/fb_max）；漂移是通用机制问题 | M0-M1 spike 验证 |
| Linux 低延迟 | PREEMPT_RT 已并入主线（6.12+）；**128 帧@48k 往返约 6-8ms（8ms 底线档）；64 帧 ≈2.7ms 缓冲（5ms 目标档）** | 5ms 档 = 64 样本 + RT 调优 |

## 6. 工程红线（违反 = 返工）

1. **音频线程实时安全**：回调内零堆分配/零锁/零 I/O/零异常。参数走无锁 SPSC 队列 + 平滑。
2. **core 音频路径零第三方依赖**（只允许 std::）；预设加载/JSON 序列化路径允许 header-only 豁免（nlohmann/json，D25）；**电路建模路径允许 WDF 库豁免**（WaveDigitalFilters，GPL-3.0 固定 commit，仅限 `core/modules/dsp/` 内使用，#111）。JUCE 只出现在 `desktop/`。SIMD 编译选项只允许出现在 NAM 库 target（`core/modules/nam/`），core 本体不得含。
3. **图交换协议**：所有音频图变更 = request → 后台加载 → 双缓冲原子交换。UI 线程直接改图 = 错误。
4. **注册表唯一键**：模块 ID 全局唯一；预设加载逐槽显式校验并报错，不静默失败。
5. **引擎是参数权威**：UI 只读镜像，经队列订阅引擎广播。
6. **术语纪律**：写代码/文档前查 `CONTEXT.md`。禁用"快照/演出场景/音色库/坑位"等废弃词（历史评审记录除外）。
7. **预设 schema**：改动必须走迁移链（v1 起），不得静默改字段。
8. **测试先行**：每个模块 = 单元 + 回归 + 参数空间扫描；黄金输出用相对误差+频域双重断言，跨平台阈值分档。

## 7. 已知坑（先看再动手）

- **UAC2 双时钟域**：USB SOF 时钟 vs I2S 本地晶振漂移 → 周期性爆音。解法：异步反馈端点（c_sync=async）+ 24h 漂移测试。
- **NAM lazy init**：预热前（加载后后台跑哑输入）回调内分配是经典炸点。M4 必须预热 + 分配器检测。
- **AK4458 是纯 DAC**（无 ADC）。吉他输入路径必须有 ADC（外置 AK5558/CS5340 或复用 RK3308 内置 8ch ADC）。
- **"地分割"是反的**：统一地平面 + 模拟供电滤波 + 单点星型汇接；割地 = 缝隙天线 + 地弹。
- **关机 pop**：9V 跌落瞬间 codec/耳放 pop 是第一投诉项 → 掉电检测 + 储能窗口内 mute 序列。
- **A2 训练采样率固定**：96k 档 NAM 需内部重采样或 48k 域处理（**M4 交付，M4 开头 spike 定**）。
- **持久化写盘**：所有持久化写入 temp → fsync(file) → rename → fsync(dir) → 读回校验；ext4 上 rename 不保证断电原子性，空文件比坏文件更隐蔽。现场状态记忆变更后 debounce 保存（2-5s），断电窗口只 mute 不写文件。
- **LCD 中文**：字符型 LCD 无法渲染 CJK；低端档用图形点阵屏 + 中文字库（8 个汉字 ≥128px 宽，选型时看分辨率）。
- **性能仪表瞬态**：场景预加载瞬态读数忽略，防演出误告警。
- **CC 7-bit 量化感**：用 14-bit（MSB/LSB）+ 每源每参数平滑（CC 10-20ms / 踏板 1-3ms / whammy 专用低延迟通道）。
- **本机网络（Windows）**：GitHub 直连曾被重置、代理（clash 127.0.0.1:7897）限速 ~150KB/s → FetchContent 克隆可能卡死。对策：
  - git 全局代理配置在 127.0.0.1:7897；**勿中途 kill configure**（僵尸 git 锁死 `_deps` 目录，出现时清进程 + 删 `*.lock`）
  - 依赖已本地化：configure 时带 `-D FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=D:/study/project/nam/build/nlohmann-v3.11.3 -D FETCHCONTENT_SOURCE_DIR_WAVEDIGITALFILTERS=D:/study/project/nam/build/debug/_deps/wavedigitalfilters-src`（已持久化在 cache；**新建 build 目录需重传**）
  - 项目已设 `UPDATE_DISCONNECTED TRUE`；CI（GitHub Actions）网络正常不受影响
- **WaveDigitalFilters 集成方式**：header-only INTERFACE target（跳过其 CMakeLists 的 JUCE 示例），固定 commit `f3917749`，仅 `core/modules/dsp/` 电路建模路径可用（红线 2 豁免 #111）

## 8. 里程碑总览（当前 = M1 软件完成，M2 待启动）

| 里程碑 | 内容 | 验证 | 预估 |
|---|---|---|---|
| **M0（已完成）** | 骨架：git + monorepo + CMake + CI + 实时安全框架 + RK3308 ev 板采购 + UAC2 spike 启动 + 生态调研 | 音频直通无爆音；回调分配计数器生效 | 2-3 周 |
| M1 | 音色链核心 + 预设存取（nlohmann 豁免）+ 迁移 + 备份 + **RK3308 Linux aarch64 交叉编译（原样编译）** + UAC2 spike ≥8h | **全 DSP 演示链出声**（NAM+IR 出声验收移 M4）；软件部分已完成，硬件验收项待板 | 3.5-4.5 周 |
| M2 | DSP 库（**名单块复刻 #110** + 音高家族单音） | 单元+回归+参数扫描 | 8-12 周 |
| M3 | IR 引擎（UPOLS + 重采样） | 误差 < -100dB | 2-3 周 |
| M4 | NAM 集成 + A2 bake-off（定主力芯片） | .nam 出声；双端一致 | 3-4 周 |
| M5a/b/c | 桌面编辑器（+导出/导入+30+ 演示预设）/ 场景+输出 / 调音器+MIDI+WASAPI+演出基础 | <5ms（64 样本档）；场景无爆音 | 10-15 周 |
| M6 | VST3/AU 插件 | DAW 可加载 | 2-3 周 |
| M7a/b/c | 嵌入式核心(v1 承诺) / I/O 扩展 / 连接升级 | 5ms + 24h soak | 13-18 周 |
| M8 | H7 低端档研究 | DSP+IR 跑通 | 研究性 |

砍序（超预算时）：M8 → M7c OTA → M7b → M5c 部分。

## 9. 当前任务：M2 第一批 — TS808 风格过载（电路级 WDF）

> 开工顺序：读本文档 §3-§7（环境/红线/坑）→ 读 `docs/research/ts808.md`（电路事实）→ 按下方步骤实现。

### M2-1 TS808 风格过载（~2-3 天）
- [ ] `core/modules/dsp/wdf/` 封装 WaveDigitalFilters（已接入，仅此路径可用，红线 2 豁免 #111）
- [ ] `ts808.h/.cpp`：信号链 = 输入缓冲 → 非反相运放削波级（反馈环 1N914 对管 WDF 子网，Zf=(51k+500k Drive)∥51pF）→ 音调（723Hz 低通 → 20k 电位器 → 220Ω+0.22µF，BLT 双二阶）→ 音量 → 输出；9V/4.5V 偏置；2× 过采样；参数 Drive/Tone/Level（元件值与系数见 `docs/research/ts808.md`）
- [ ] 注册 `registerTs808(ModuleRegistry&)`，模块 ID 如 `od.ts808`（改名规避商标）
- [ ] 测试：单元（削波特征/增益范围 12-118×/频响）+ **参数空间扫描**（Drive×Tone×Level 网格：输出有限/无 NaN/无爆音）+ rt_alloc guard
- [ ] 演示预设 `core/preset/demo/` 增加 1 个 TS 风格链（与现有 gain/tone 混排）
- [ ] 验证：debug+release ctest 全绿 → CI 4 job 全绿

### M2-2 后续（每块同法，见 PLAN §6 映射表）
过载家族（Klon/OCD 风格）→ 压缩（Dyna Comp 风格）→ 调制（CE-2/Phase 90/BF-2/Crybaby）→ 延迟（DM-2/Echoplex）→ 混响（弹簧/Hall）→ 门限（NS-2）→ 音高家族（移调核心 → 八度/whammy/和声器，算法路线）

### M2 验收
单元+回归+参数空间扫描通过；移调核心可演出级；每块模块 ID 全局唯一 + 预设逐槽校验。

---

## 10. M0 执行清单（已完成，留档）

M0 目标：**项目骨架站起来，验收 = 音频直通无爆音 + 回调分配计数器生效**。预估 2-3 周。

### T1 仓库初始化
- [x] `git init` + 首次 commit（.gitignore：build/、.vs/、.vscode/、CMakeUserPresets.json、*.user）
- [x] 按 PLAN §4 建目录骨架：`core/{audio,modules,preset,perf,platform}`、`desktop/`、`embedded/{linux,mcu}`、`tools/{latency,perfbench}`、`tests/{unit,regression,fuzz}`、`docs/`
- [x] 根 `README.md`：项目一句话 + 文档地图（指向 EXECUTION/PLAN/CONTEXT/TODOS）
- [x] 根 `AGENTS.md`：本文件 §3/§6/§7 的精简版（环境、红线、坑），供后续 agent 快速对齐

### T2 CMake 骨架
- [x] 顶层 `CMakeLists.txt`：C++17、项目名 namfx、`add_subdirectory(core)` 等
- [x] `core/CMakeLists.txt`：静态库目标 `namfx_core`，`-Wall -Wextra -Werror`（MSVC 对应 /W4 /WX，经共享 `namfx_warnings` INTERFACE target），无任何第三方依赖
- [x] `CMakePresets.json`：debug/release × 本地 Windows（VS 2026）/ Linux / CI（ci-win × VS 2026、debug-linux 等）
- [x] 验证：空库可构建

### T3 实时安全检测框架（M0 验收项之一）
- [x] `core/platform/rt_alloc.h`：音频线程分配检测——全局 new/delete 计数钩子（debug 构建启用，含 nothrow/aligned 变体）+ `rt_assert_no_alloc()` RAII 哨兵（进入回调置标志，回调内任何分配触发断言/记录；嵌套安全）
- [x] 注：core 自身在回调外仍可正常用 std::；**音频回调内**由哨兵保证零分配
- [x] `tests/unit/rt_alloc_test.cpp`：① 回调外分配通过 ② 回调内分配触发检测（证明框架有效）
- [x] 验收：`ctest` 全绿，且故意违反的测试证明检测真能抓到

### T4 音频直通（M0 验收项之二）
- [x] `core/audio/`：最小 `AudioGraph`（双缓冲交换的雏形：`commit()`/`swap()`）+ 直通 `processBlock(in, out, n)`（逐样本拷贝）
- [x] `tools/latency/engine_latency.cpp`：脉冲注入直通图，测引擎处理延迟（CLI 输出 ns）——这是 CI 唯一延迟断言的工具
- [x] `tests/unit/audio_graph_test.cpp`：直通逐样本一致（double 精度断言）
- [x] 验收：直通测试绿 + 引擎处理延迟 < 0.5ms（实测 4-26 ns）

### T5 测试框架接入
- [x] Catch2 v3 经 FetchContent 接入 `tests/unit/`（v3.8.0 固定 tag，仅测试目标依赖，core 保持零依赖）
- [x] `ctest --preset debug` 全绿
- [x] 第一个 fuzz 占位目录（`tests/fuzz/README.md` 说明故障注入目标清单，M1+ 填充）

### T6 CI（M0 验收项之三）
- [x] GitHub Actions：`windows-latest` + `ubuntu-latest` 双平台 build + ctest（debug/release 各双 job，4 矩阵全绿）
- [x] 延迟断言：跑 `engine_latency`，阈值 < 0.5ms（CI 只断言引擎延迟，端到端人工跑）

### T7 硬件采购与 UAC2 spike 启动
- [ ] 采购：**RK3308 ev 板**（Firefly ROC-RK3308-CC / 芯板类，~¥100-150，内置 8ch ADC + 2ch DAC codec，USB OTG 支持 UAC2 gadget）——用户下单
- [ ] 到货后：点灯测试 → 内置 codec I2S 环回（回环播录）→ 记录第一手延迟数据（48k 域）
- [ ] UAC2 spike（跨 M0-M1）：RK3308 Linux UAC2 gadget（configfs）枚举为声卡，录放观察漂移；**M1 验收 = spike 跑通 + ≥8h 初步数据，24h 结论移 M2**；产出 `docs/research/uac2-spike.md`（结论 + 数据 + 对 M7c 的启示）
- [ ] 注：图形点阵 LCD（中文全端显示）为低端档（M8）需求，主力档 UI = Web UI，M0 不采购

### T8 生态调研
- [x] moddsp/modep（MOD Devices，开源嵌入式+桌面 LV2 踏板链）半天调研：与本项目重合度、是否有直接省里程碑的路径
- [x] 产出 `docs/research/modep.md`（结论 + 是否借道 + 借鉴点）——结论：partial，不借道（GPL 传染+JACK 栈破坏三条红线），借鉴 10 条（mod-host 命令总线等）

### T9 收尾
- [x] M0 验收三件套跑通：音频直通无爆音 ✓ 回调分配计数器生效 ✓ CI 双平台绿 ✓（run 31669327486，4/4 job success）
- [ ] 更新 `docs/PLAN.md`（§13 里程碑表勾 M0）——留待 M1 启动简报时一并更新
- [ ] 进入 M1 前向用户简报：直通延迟数据 + RK3308 首测数据 + UAC2 spike 初步结论（阻塞于 T7 硬件到货）

## 11. 需要澄清时问什么

以下问题计划尚未定死，遇到时**问用户**（不要自己拍板）：

1. 代码仓库托管位置（GitHub/Gitee/本地）与 CI 平台
2. 项目名/产品名（代码暂用 `namfx`，可改）
3. ~~STM32H7 具体开发板型号偏好~~ → 已定：RK3308 ev 板（#109，用户 2026-08-13 改判）
4. ~~开源还是闭源~~ → 已定：**整体 GPL-3.0**（#111，2026-08-13 用户拍板；引入 WaveDigitalFilters，红线 2 修订）
5. 中文市场定位是否需要正式市场文档（`docs/PLAN.md` §15）
6. 桌面 I/O 设备管理 UI 形态——已闭合（#88：WASAPI 独占/共享切换 + 设备断连自动重连/回退共享，M5 交付）

除此之外，**不要**拿 §4 决策表里的内容问用户。
