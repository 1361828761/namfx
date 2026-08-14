# NAM 集成调研（NAM Core 封装：加载/A1/A2/采样率策略）

> 用途：M4 NAM 集成（PLAN §6/§13：NAM Core 独立静态库、A1/A2 Full/Lite 加载、微调参数 gain/三段EQ/output、96k 策略 spike #18/#105）。
> 原则：所有数值来自下方引用来源或本机 spike 实测（标注"本机实测"）；凭记忆推测的数值一律不写。
> 调研日期：2026-08-14。

## 0. 结论摘要

- **依赖**：官方 `NeuralAmpModelerCore`（sdatkinson，MIT）——C++20、自带 Eigen（MPL2）+ nlohmann json.hpp（MIT）依赖，无 AudioDSPTools 硬依赖（仅官方 render 工具用其 wav.cpp，本项目自写 wav 解析）。tarball 802KB + Eigen 3.4.0 2.7MB，代理可下（本机已本地化 `build/_deps/`）。
- **构建**（本机实测，MSVC/VS18 2026，Release）：NAM/*.cpp + NAM/*/*.cpp 编译为静态库 `nam_core`（C++20、`NAM_SAMPLE_FLOAT`、eigen SYSTEM include）→ **链接必须 `/WHOLEARCHIVE:nam_core`**（架构 parser 是匿名命名空间静态注册对象，MSVC 静态库未引用 obj 被丢弃 → "No config parser registered"，官方 tools 直接编源码入 exe 规避；本机 spike 已用 /WHOLEARCHIVE 解决）。
- **API**：`nam::get_dsp(path)` → `unique_ptr<nam::DSP>`（加载路径，可抛 NamFileValidationError）；`DSP::Reset(sampleRate, maxBufferSize)`（默认含 prewarm）、`DSP::process(NAM_SAMPLE** in, NAM_SAMPLE** out, frames)`、`GetExpectedSampleRate()`（缺省 48k）、metadata（loudness/input_level_dbu/output_level_dbu）由 get_dsp 自动应用。
- **架构支持**：WaveNet / ConvNet / LSTM / Linear / SlimmableContainer / SlimmableWaveNet（A2 slimmable 切片）；A2 fast-path（`NAM_ENABLE_A2_FAST` 默认 ON，config 形状匹配 A2 时走手优化实现）。
- **.nam 格式**：JSON（version/architecture/config/weights[]，weights 为扁平 float 数组，架构相关映射）；可选 sample_rate（缺省 48k）、metadata。版本支持 0.5.0 - 0.7.0。
- **性能**（本机实测，x86 Release，17.3s 吉他干声，块 512）：
  | 模型 | 渲染耗时 | 实时倍率 |
  |---|---|---|
  | wavenet.nam（标准 A1） | 115.8ms | 149.6x |
  | A2.nam（SlimmableContainer） | 1765.7ms | 9.8x |
  | wavenet_a2_max.nam（A2 8ch） | 1072.8ms | 16.1x |
  | slimmable_wavenet.nam（Lite） | 233.3ms | 74.2x |
  → x86 余量巨大；**A2 Full 档约 10-16x，Lite 档约 74x**——RK3308（Cortex-A53 ~1/10-1/20 性能）上 A2-Full 约 0.5-1.5x 实时、Lite 约 4-7x，bake-off 数据预判与 PLAN §13"Lite 档大概率可行"一致。
- **96k 策略（spike 决策 #18/#105）**：**引擎率 ≠ 模型率时模块内部流式重采样**（windowed-sinc ZC=64/β=14，M3 同款核，流式化），而非 48k 域整链处理——因为：①模型 48k 固定（A2 训练率），②模块级重采样保持引擎全局采样率不变（其他 DSP/IR 模块不受影响，IR 已按引擎率重采样），③重采样成本相对 NAM 推理可忽略（ZC=64 FIR ~3M MAC/s）。44.1k 引擎 + 48k 模型同理。
- **C++ 标准隔离**：core 保持 C++17 红线不变；`nam_core` 独立静态库 target 用 C++20（MSVC 无 ABI 问题）；模块封装 `nam_amp` 的**头文件不含任何 NAM 头**（C++20 不泄漏），实现全在 .cpp。
- **实时安全**：get_dsp/Reset/prewarm 全在加载线程（Chain 构造/prepare 路径）；process 回调只调 `DSP::process`（NAM Core 文档声明预分配零分配，rt_alloc 测试锁定）。prewarm 由 Reset 默认执行（A2 实测 15.7ms，加载路径可接受）。

## 1. 事实与来源

### 1.1 NAM Core（官方）

- 仓库：https://github.com/sdatkinson/NeuralAmpModelerCore（MIT）——"high-performance C++ library ... WaveNet/ConvNet/LSTM/Linear"，实时安全（预分配）、Eigen 线性代数
- 版本支持：EARLIEST 0.5.0 / LATEST_FULLY_SUPPORTED 0.7.0（get_dsp.h 常量）
- 依赖：Dependencies/eigen（gitlab submodule，MPL2）、Dependencies/AudioDSPTools（仅 render 工具）、Dependencies/nlohmann/json.hpp（自带，MIT）
- CMake 选项：`NAM_ENABLE_A2_FAST`（A2 fast-path，默认 ON）、`NAM_SAMPLE_FLOAT`（float 推理，默认 double）、`NAM_DEFAULT_MAX_BUFFER_SIZE`（默认 4096）

### 1.2 .nam 文件格式

- 官方规范：https://neural-amp-modeler.readthedocs.io/en/stable/model-file.html —— JSON：version/architecture/config/weights（扁平 float 数组）；可选 sample_rate（缺省 48k）、metadata（name/modeled_by/gear_*/input_level_dbu/output_level_dbu/loudness 等）
- A2 slimmable：切片语义（_SlimmableConvLayer 等按 adjust 比例切片权重），官方训练器产出

### 1.3 集成要点（PLAN 红线对照）

| PLAN 条款 | 落地 |
|---|---|
| §6 NAM Core FetchContent + 固定 commit，不用 submodule | FetchContent + `FETCHCONTENT_SOURCE_DIR_NAM_CORE` 本地覆盖（本机已本地化，新建 build 需重传；UPDATE_DISCONNECTED） |
| §6 隔离独立静态库，SIMD 只在该 target | nam_core target；C++20 仅该 target；/WHOLEARCHIVE 链接 |
| §41 加载层兼容 A1/A2/LSTM 全部 .nam | 直接用官方 get_dsp（注册表含 WaveNet/Slimmable/ConvNet/LSTM/Linear） |
| §42 性能档位 = 控制加载架构 + A2 档位选择 | v1 全档加载（不降档）；A2 Lite 档选择 = 控制加载（模型文件含子模型，SlimmableContainer 选 submodel）——v1 加载默认子模型，档位切换 UI 后置（M5） |
| §44 微调参数 gain/三段EQ/output | amp.nam 参数：gain（输入前）/bass/middle/treble（输出后 EQ）/output（输出总增益） |
| #27 NAM 预热 + 分配器检测 | Reset 默认 prewarm；rt_alloc 测试锁定 process 零分配 |
| #18/#105 96k 策略 | 模块内部流式重采样（本 spike 决策） |

## 2. 本机 spike 验证（build/nam-spike/）

- 依赖本地化：`build/_deps/NeuralAmpModelerCore-main`（tarball 802KB）+ `Dependencies/eigen`（eigen-3.4.0.tar.gz 2.7MB）+ nlohmann（自带）
- 编译：VS18 2026 x64 Release，`NAM_SAMPLE_FLOAT`，静态库 + /WHOLEARCHIVE —— ✅ 成功
- 加载 4 个例模型（wavenet.nam / A2.nam / wavenet_a2_max.nam / slimmable_wavenet.nam）✅ 全部成功（expected rate 48000 或 -1）
- 17.3s 真实吉他干声渲染 ✅ 输出有限非零；性能数据见 §0
- 注意：`wavenet_a1_standard.nam` expected rate = -1（旧格式无 sample_rate 字段）→ 集成时按 48k 处理

## 3. 待落地项（v1 之后）

1. **输入电平自动补偿**：metadata input_level_dbu/output_level_dbu 已由 get_dsp 应用（SetInputLevel 等），但 DSP 内部不自动用 input level 缩放输入——官方插件侧做 0dBFS→dBu 映射；v1 用 gain 参数手动，后置自动映射
2. **A2 档位 UI**：SlimmableContainer 子模型选择（Full/Lite）控制加载——v1 默认子模型，UI 后置（M5 参数面板）
3. **loudness 输出归一化**：模型 loudness 元数据已 Set，是否自动补偿输出待产品决策（默认不补偿，output 参数手动）
4. **LSTM 架构性能**：example_models/lstm.nam（2.3KB）未测——集成后补测
5. **多实例并发**（PLAN §13：同预设 2-3 NAM 实例压力）——集成测试补
6. **A2 bake-off**（RK3308，PLAN §13）——等板子，性能预判见 §0
