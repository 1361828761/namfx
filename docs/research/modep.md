# MOD Devices / 开源效果器宿主生态调研（M0-T8）

> 调研日期：2026-08-13
> 范围（v0.7 #73 扩展）：MOD Devices（mod-host / mod-ui / MODEP）→ Guitarix → NAM 社区宿主（官方插件 / NAM-LV2 / ToneLib GFX / MOD 生态）→ 附：Elk Audio SUSHI
> 调研方式：GitHub API / README / 官网，只读调研，无代码拉取
> 关联决策：PLAN §2 决策 #7；本调研结论回填 TODOS P1 moddsp 条目

## 1. 结论（TL;DR）

**partial——没有可直接省 3 个里程碑的借道路径，但有一份高价值"抄作业"清单。**

MOD 生态在形态上与 namfx 嵌入式档重合度极高（Linux SBC + Web UI + 踏板链 + 300+ 插件生态），官方 MOD Dwarf 甚至已经在卖"ARM 硬件踏板 + NAM 插件"。但直接借道不成立，三个硬理由：

1. ~~**许可冲突**：mod-host / mod-ui 均为 GPL-3.0，与 namfx"可能闭源商用"（PLAN §15 开放问题 4）冲突；设备侧整机代码会被 GPL 传染~~ **（2026-08-13 决策 #111 后失效：namfx 已整体转 GPL-3.0，与 MOD 生态许可兼容；架构冲突仍独立成立）**
2. **架构冲突**：MOD 架构 = 每个插件一个独立 JACK 客户端进程 + 文本 socket 协议。多进程/JACK 依赖栈与 namfx"单进程纯 C++17 引擎、5ms 预算、core 零第三方依赖、H7 可移植"的工程红线抵触，且嵌入式专用设计会反向污染 core（违反 D24 冻结原则）。
3. **能省的不是要自研的**：MODEP 只提供"踏板板外壳"（宿主 + Web UI + 插件市场），不提供 namfx 真正要自研的引擎（NAM 封装、IR、DSP 模块、统一预设格式）。借道省掉壳的成本，但引擎一个里程碑都省不掉——而引擎正是 namfx 的立身之本与差异化所在。

真正的价值在借鉴：**NAM 已有 LV2 移植（mikeoliphant/neural-amp-modeler-lv2，520★）在 MOD Dwarf / MODEP / Pi 3 上实跑**，直接验证"ARM SBC 跑 NAM"的可行性，把 M4 A2 bake-off 的不确定性再降一档；mod-host 的 socket 命令协议、mod-ui 的连接状态机、pi-stomp 的嵌入式三端 UX、Guitarix 的 DSP 算法（干净室参考）、ToneLib GFX 的练习闭环与预设生态，都是零成本可吸收的设计输入。详细清单见 §4。

## 2. 各项目分析

### 总览表

| 项目 | 架构 | 许可 | 复用价值 | 与 namfx 重合度 | 结论 |
|---|---|---|---|---|---|
| mod-host | C；LV2 宿主；每插件 = 独立 JACK 客户端进程；文本 socket 协议（默认 5555 端口） | GPL-3.0 | 协议命令集可参考；代码不可并入闭源 | 中（嵌入式宿主层重合，引擎不重合） | 不借道，抄协议 |
| mod-ui | Python（Flask/Tornado + WebSocket）+ Web 前端；浏览器 ↔ mod-ui ↔ mod-host | GPL 系 | 连接状态机、图编辑交互参考 | 中 | 不借道，抄交互 |
| MODEP（BlokasLabs） | mod-ui + mod-host 打包成 Patchbox OS 模块；Patchstorage 插件云 200+ LV2 | GPL-2.0（打包脚本） | 验证 Pi 上整套栈可跑；生态脆弱 | 中 | 可选做探针 |
| MOD Dwarf / MOD Desktop（商业） | ARM 硬件踏板 / 免费桌面应用；同一 Web UI 平台；商店 300+ 插件含 NAM | 硬件闭源，宿主开源 | **直接竞品 + 存在性验证**；UX 基准 | 高（产品形态几乎相同） | 竞品研究 |
| Guitarix | C++；gx_head 引擎（JACK 客户端）+ GTK3 UI + LV2 套件导出；25+ 模块；Eigen/FFTW/Boost/zita | GPL-2.0 | DSP 算法干净室参考；代码不可抄 | 中（M2 DSP 库范围重合） | 抄算法思想 |
| NAM 官方插件（sdatkinson） | JUCE；VST3/AU/AAX；NAM Core 封装 | MIT | NAM Core 已定复用；UI 惯例参考 | 部分（namfx 不做独立 NAM 插件） | 参考 UI |
| neural-amp-modeler-lv2（mikeoliphant） | C++；NAM 的 LV2 移植；520★，活跃 | GPL-3.0 | **ARM 可行性实证**；MODEP 探针素材 | 低（namfx 用官方 Core） | 验证用 |
| ToneLib GFX（商业） | 闭源；VST/VST3/AU/独立；Win/macOS/Ubuntu；80+ 设备、500+ IR、在线预设库；支持加载 NAM | 商业（$44.98-89.95） | 交互/练习工具/预设生态参照 | 高（直接竞品，免费试用档） | 竞品研究 |
| pi-stomp（TreeFallSound） | Python；基于 MODEP 的 DIY 单块平台；脚踩/LCD/Web 三端；168★，非常活跃 | AGPL-3.0 | 嵌入式硬件 UX 参照 | 中 | 抄硬件 UX |
| Elk Audio SUSHI | C++；Elk Audio OS 的插件宿主+DAW；VST2/VST3 + 内部插件；gRPC/OSC 控制 | 自定义"Other"（非标准 OSS，GitHub 标注 NOASSERTION） | 控制协议设计参考 | 中 | 参考协议 |

### MOD Devices / mod-audio（原 moddevices，2023 年更名 + 改名 MOD Audio）

架构是三层：**mod-host**（C 写的 LV2 宿主，每个插件实例 = 一个独立 JACK 客户端进程 `effect_N`，崩溃隔离靠进程边界；通过默认 5555 端口接受文本命令：`add/remove/connect/disconnect/param_set/param_get/bypass/preset_load/preset_save/midi_learn/midi_map/cc_map/cv_map/cpu_load/transport`，应答格式 `resp <status> [value]`，负数为错误码）→ **mod-ui**（Python Web 服务，浏览器经 WebSocket 连接，再转译成 mod-host 命令）→ **mod-sdk**（插件 GUI 描述规范）。支持 LV2 worker/atom/state/presets 特性，依赖 JACK + lilv，ARM 与 x86 均可编译（MOD Desktop 即 PC 版证明）。延迟由 JACK 缓冲档位决定，无 5ms 级承诺；多进程架构带来上下文切换与每插件 JACK 传输开销，这是它没做 5ms 卖点的原因之一。许可 GPL-3.0 是 namfx 不可跨越的墙。

### MODEP（Blokas Labs，树莓派版 MOD）

Blokas Labs 把 mod-ui + mod-host 打包为 **Patchbox OS 的模块**，面向 Raspberry Pi + Pisound 声卡，插件库走 Patchstorage 平台（200+ LV2 插件，arm32，可从 MODEP UI 内一键安装）。许可证 GPL-2.0。价值：证明整套 MOD 栈在 Pi 级 ARM 上开箱可用，且存在 NAM 类神经插件（GuitarML modep-plugins、DarkStar 等）。风险：维护力量薄弱——modep-debs 打包仓库半年未更新，属于"社区维持"而非"活跃开发"。

### MOD Dwarf / MOD Desktop（商业产品线，竞品）

MOD 公司自身的商业化形态：**Dwarf** = ARM Linux 硬件踏板（脚踩开关 + Web UI 编辑 + 插件商店 300+，官方商店明确上架 NAM / AIDA-X 等神经放大模型插件），**MOD Desktop**（2025 推出）= 免费桌面宿主，与硬件同一 Web UI 体验。对 namfx 的意义是双重：负面 = 这是最直接的竞争对标（形态、定价段、插件生态打法几乎逐条对应 namfx 计划）；正面 = 它证明了"Web UI + ARM 踏板 + NAM"这条路**市场已验证可行**，namfx 的差异化只能靠引擎质量、价格（SBC BOM 1/5-1/10）、预设生态与中文市场。

### Guitarix

20 年老项目，GPL-2.0，JACK 生态，GTK3 UI（gtkmm），waf 构建。架构 = **gx_head 无头引擎**（mono 输入 → stereo 输出，独立 JACK 客户端）+ rack 式模块（噪声门、压缩、EQ、延迟、混响、flanger、phaser、auto-wah 等 25+ 模块）+ 箱体卷积（zita-convolver/zita-resampler）；同时导出完整 **LV2 插件套件**供 DAW/其他宿主使用（其插件已进入 MOD 官方商店，证明该代码在 ARM 嵌入式实跑）。官方宣称配置得当的 Linux 上 <10ms。对 namfx：**M2 DSP 库的算法教材**——GPL 代码一行不能抄，但其模块清单、参数集与电路思路（尤其电子管前级模型）是极好的干净室参考；其"引擎与 GUI 分离（gx_head ↔ libgxw）"正是 namfx"core 与壳分离"的既有先例。注意其代码风格为长期单人维护，工程质量参差，参考思想、不参考实现。

### NAM 社区宿主

- **官方 NeuralAmpModelerPlugin**（MIT，2925★，2026-08 仍在更新）：JUCE 实现，VST3/AU/AAX。UI 惯例：拖拽 .nam 文件直接加载、模型信息卡（架构 A1/A2/LSTM、采样率）、输入/输出电平表、gain + 3 段 EQ + 噪声门作为 NAM"微调件"、内置 IR 加载器。**这是 namfx NAM 模块参数面板的直接蓝本**（与 PLAN 已定 gain/三段EQ/output 一致，补充：电平表 + 模型元信息展示）。
- **neural-amp-modeler-lv2**（mikeoliphant，GPL-3.0，520★，活跃）：NAM 的 LV2 移植，可加载于 mod-host / MODEP / MOD Dwarf。**实证意义大于复用意义**：NAM 推理在 Pi 3（MODEP）与 Dwarf（ARM Linux）实跑是社区公开事实，M4 bake-off 的"A2-Lite 在 Cortex-A 可行"从"大概率"升级为"有同栈实装先例"（注意：实装多为 A1/WaveNet 档，A2 Full 仍需 namfx 自己实测）。
- **ToneLib GFX**（商业，$44.98-89.95，30 天试用）：闭源 VST/VST3/AU/独立应用，Win/macOS/Ubuntu 三平台，80+ 设备（22 箱头 + 60+ 单块 + 500+ IR），支持加载 NAM 模型，**内置 IR 处理器 + Splitter 平行分支**。其独立应用形态（免 DAW）+ 练习工具全家桶（调音器/节拍器/鼓机 99 pattern/伴奏播放/变调变速/looper/录音）+ 在线预设库（150+ 预设）正是 namfx"丢进来就能弹"用户旅程的完整参照系，也是 M5 演出基础与 TODOS 练习辅助（P2）的竞品基准。
- **GuitarML modep-plugins / DarkStar**（GPL-3.0）：GuitarML 系神经网络插件（SmartGuitarAmp 等）专门为 MODEP Pi 3 移植的 LV2 版本——再次印证 ARM 神经放大已进入 MODEP 生态。

### Elk Audio SUSHI（附）

Elk Audio OS（商业嵌入式音频 OS）的开源宿主 **SUSHI**：C++ 插件宿主 + DAW，跑 VST2/VST3 与内部插件，通过 gRPC + OSC 双协议被外部控制，专为低延迟嵌入式 Linux 设计（RT 内核、跨编译到 ARM 板）。许可为自定义"Other"（GitHub 标注 NOASSERTION，非标准开源许可），对闭源商用不友好。与 mod-host 是"VST 生态 vs LV2 生态"的同构竞争者。对 namfx 的可取处：**控制协议设计**——gRPC（结构化、强类型）与 OSC（轻量、低带宽）双通道分工，对 M7c Web UI 控制总线与未来移动端控制有参考价值。

## 3. 与 namfx 重合度结论

形态重合（嵌入式宿主、Web UI 编辑、踏板硬件、插件/预设生态）≈ 80%，**技术内核重合（引擎、预设格式、双端同步）≈ 0%**。所有调研项目的引擎要么是"插件宿主"（mod-host/SUSHI），要么是"固定模块吉他放大"（Guitarix），要么是"单模型播放器"（NAM 插件）；没有任何一家在做"NAM + IR + 白盒 DSP 三技术 + 统一 JSON 预设 + 双端一致"的引擎。这解释了结论：**借道拿不到引擎，借鉴却处处有素材**。

## 4. 可借鉴点清单（具体到设计/架构/交互）

1. **引擎控制命令总线（抄 mod-host）**：mod-host 把图编辑、参数读写、旁路、预设加载/保存、MIDI 学习、CV 映射、CPU 负载查询全部做成幂等文本命令 + `resp <status> [value]` 应答、负数错误码表。namfx 的引擎↔壳边界（M7c Web UI 控制源）应定义同构的命令总线：UI 永不直接改图，一切经命令总线进引擎仲裁——与 PLAN 红线 3/5 完全一致，mod-host 的完整命令集就是现成的命令清单模板。
2. **Web UI 连接状态机（抄 mod-ui）**：CONNECTING / RECONNECTING / STALE 三态 + 重连后只读横幅 + 最后已知状态快照——PLAN v0.7 #79 已定同名三态，直接吸收 mod-ui 的交互细节（断连期间 UI 冻结 vs 标记 stale 的处理）。
3. **图编辑器交互（抄 MOD Web UI）**：模块卡 + 参数面板 + 拖拽重排/连线 + 每模块 CPU 占用指示。namfx 定为链式 ≥8 槽位而非自由图，但"模块卡片 + 展开参数 + 拖拽排序 + 旁路/干湿比"的交互骨架可逐项对照。
4. **嵌入式三端 UX（抄 pi-stomp + MOD Dwarf）**：脚踩长按/双功能状态机、小屏页面层级、WiFi 配网向导、演出锁定视图——与 PLAN §11"日常操作"逐条对应，pi-stomp 的硬件面板定义验证了 namfx 设计方向，其 AGPL 代码不可抄但交互可抄。
5. **DSP 模块算法教材（抄 Guitarix 思想）**：M2 实现噪声门/压缩/EQ/延迟/混响/调制时，用 Guitarix 25+ 模块做"读代码 → 定位算法来源（论文/教科书）→ 独立实现"的干净室流程；参数集与模块边界直接参照其 rack 结构。
6. **NAM 模块参数面板（抄官方插件惯例）**：拖拽 .nam 加载 + 模型信息卡（架构/采样率/名称）+ 输入输出电平表 + gain/三段EQ/噪声门微调件——PLAN 已定微调参数，补充电平表与元信息展示，照官方插件交互即达用户预期。
7. **练习闭环与预设生态（抄 ToneLib GFX）**：Audition 工具（录 riff 循环、双手调参）、免 DAW 独立应用定位、大图块预设浏览 + 在线预设库——是 namfx 用户旅程（丢进来就能弹）与预设分享生态（差异化押注点）的现成参照。
8. **模块进程隔离思想（抄 mod-host 的动机，不抄机制）**：MOD 用进程边界换崩溃隔离；namfx 拒绝多进程（5ms 红线），但"NAM 模块预热 + 分配器哨兵 + 过载降级隔离"是同一动机的进程内等价物；未来第三方模块扩展（TODOS P3）时重新评估进程外隔离。
9. **控制协议双通道（抄 SUSHI）**：结构化通道（gRPC 式，调试/桌面用）+ 轻量通道（OSC 式，演出实时控制用），为 M7c 控制总线与移动端控制预留。
10. **实证数据（抄 MODEP/Dwarf 的验证结论）**：NAM-LV2 在 Pi 3 与 Dwarf 实跑 = "ARM SBC 跑 NAM"已有公开先例，M4 bake-off 计划无需变更但预期值上调；MOD 生态 300+ 插件在 128 帧档消费级踏板上的延迟表现 = namfx 8ms 底线档的市场可接受度参照。

## 5. 风险与决策建议

**决策：不借道（直接复用 MODEP/mod-host 栈），只借鉴。** 与 PLAN §2 决策 #7 的调研意图一致：确认无省里程碑路径后关闭该选项。

若借道，代价清单（否决依据）：
- ~~GPL-3.0 传染：设备侧代码整体需开源，直接锁死"闭源商用"选项（PLAN §15 开放问题 4）~~（#111 后失效：namfx 已整体 GPL-3.0，与 MOD 生态许可兼容）
- 架构妥协：引入 JACK + LV2 + 多进程依赖栈，破坏 core 零第三方依赖、5ms 预算、"双端共享单一引擎"三条红线；嵌入式专用设计反向污染桌面（违反 D24）。
- 双格式维护：LV2 state/preset 与 namfx JSON schema 双轨并存，预设迁移链失去"双端一套格式"的意义。
- 生态脆弱：MODEP 维护薄弱（打包半年未更），Patchstorage 插件云存在未来不确性；MOD 官方重心在其商业产品线。
- 内存/CPU 开销：每插件一进程的架构在 1GB 级 RAM 板上挤压 NAM 模型与 IR 预算。

若只借鉴（推荐路径），纪律要求：
- 干净室纪律：Guitarix 等 GPL 代码只做算法思想参考，阅读-实现边界留痕（记录参考了哪份代码、定位了哪些公开文献），防 GPL 污染指控。
- 竞品纪律：ToneLib GFX / MOD Dwarf 为商业竞品，只参考交互模式，不参考视觉资产（截图布局、贴图、配色方案注意版权与商标）。
- ~~许可耦合风险：MOD 生态 GPL 与 namfx 开源/闭源决策耦合——若未来选定 GPL 开源路线，mod-host 协议文档是唯一值得回头再看的资产；当前按"闭源兼容"口径做决策（NAM Core MIT 路线不变）~~（#111 后已定：namfx 整体 GPL-3.0，mod-host 协议文档可直接参考）

折中探针（可选，半天内，M4 顺路做）：M4 A2 bake-off 的 RK ev board 上跑 MODEP 或 neural-amp-modeler-lv2 作为对照组，实测"官方 NAM Core 直跑 vs LV2 宿主路径"在同一硬件上的 CPU/延迟差，数据用于 M7a 决策背书与对外说法，不进入产品。

## 6. 参考链接

- MOD Devices / mod-audio（GitHub）：https://github.com/mod-audio/mod-host 、https://github.com/mod-audio/mod-ui 、https://github.com/mod-audio/mod-sdk
- MOD 官网（Dwarf/Desktop/插件商店）：https://mod.audio/ 、https://pedalboards.mod.audio/plugins
- MODEP（Blokas Labs）：https://blokas.io/modep/ 、https://github.com/BlokasLabs/modep-debs
- Guitarix：https://guitarix.org/ 、https://github.com/brummer10/guitarix
- NAM 官方插件：https://github.com/sdatkinson/NeuralAmpModelerPlugin
- NAM LV2 移植：https://github.com/mikeoliphant/neural-amp-modeler-lv2
- ToneLib GFX：https://www.tonelib.net/gfx-overview/
- GuitarML modep 插件：https://github.com/GuitarML/modep-plugins 、https://github.com/GuitarML/DarkStar
- pi-stomp：https://github.com/TreeFallSound/pi-stomp
- Elk Audio SUSHI：https://github.com/elk-audio/sushi
