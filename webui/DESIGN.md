# NAMFX WebUI — 设计与架构

> 本文件是 WebUI 的**设计基线**：布局、视觉、信息架构、传输协议、EXE 封装预留。
> 相关计划决策见 `docs/PLAN.md`（§10 Web UI 角色 / §11 嵌入式 Web UI）；术语纪律见根 `CONTEXT.md`。
> 本文件不修改 `docs/PLAN.md` / `docs/EXECUTION.md` / `CONTEXT.md` / `TODOS.md`（AGENTS 红线）。

## 1. 目标与定位

- WebUI 是全功能编辑界面（控制源之一，走引擎仲裁），功能要求与 JUCE 桌面编辑器（M5a/b/c）对齐，并补齐 M5 遗留项（A/B、undo/redo、导出/导入入口、性能仪表、演出锁定视图）。
- 视觉参照 **Line 6 Helix Native** 的信息架构与硬件语言：上方信号流窗口（效果链格子）+ 下方编辑区（旋钮参数）；深色硬件面板风。
- 三形态运行（同一前端 `webui/www`）：
  1. **浏览器 Demo 形态**：任何静态服务器均可运行，无构建步骤、无 CDN 依赖（字体自包含），纯前端模拟引擎走查 UI。
  2. **浏览器 WASM 形态**：`?wasm=1`，`core` 编译为 WebAssembly 后经 AudioWorklet 在浏览器内真实出声（接琴调试），见 §10。
  3. **WebView2 壳形态**：`desktop/App/webview_app.cpp`，界面即 WebUI，音频走原生 WASAPI/ASIO（正式形态），见 §11。

## 2. 架构总览

```
┌────────────────────────────────────────────────────────────┐
│ WebView2 壳 / 浏览器                                          │
│  webui/www/ 静态 SPA（index.html + css + js + wasm）          │
│    引擎抽象：MockEngine | WasmEngine（AudioWorklet）|         │
│    RemoteEngine（HTTP/SSE）                                  │
└───────────────┬────────────────────────────────────────────┘
                │ ?wasm=0 形态：HTTP POST /api/cmd + SSE /api/events
┌───────────────▼────────────────────────────────────────────┐
│ WebHost（webui/server/web_host.cpp + 原生音频适配层）           │
│  · 极简 HTTP/1.1 服务器（静态文件 + API，单文件无第三方）      │
│  · EngineHost 桥：预设/链编辑/参数/场景/输出/MIDI/调音器       │
│  · 壳侧概念：undo/redo 栈 + A/B 缓冲（引擎不背，PLAN D8）      │
│  · 模型库扫描（modles/、用户目录、EXE 旁目录）、IR 扫描、导入 │
│  · 电平/调音器/性能 10Hz 事件流（SSE，tick 不重建 DOM）        │
│  · WinMM MIDI 输入（Windows）：完成 MIDI 学习绑定              │
│  · EngineHost* 可外部注入（壳复用同一引擎实例）                │
└───────────────┬────────────────────────────────────────────┘
                │ ?wasm=1 形态：MessagePort 到 AudioWorklet
┌───────────────▼────────────────────────────────────────────┐
│ namfx_wasm（webui/wasm/，Emscripten）                        │
│  · 纯 C ABI：create/prepare/loadPreset/registerAsset/        │
│    setParam/bypass/mix/output/process/state                 │
│  · core DSP/WDF + NAM（get_dsp(json)）+ IR（parseWav(bytes)）│
└───────────────┬────────────────────────────────────────────┘
                │ EngineHost（desktop/Engine，薄壳）→ namfx_core（纯 C++17 引擎）
```

原则（对齐 PLAN 红线）：
- **引擎是参数权威**：UI 只读镜像；所有写操作 = 命令 → EngineHost → 路由/图交换，UI 从不直接改图。
- **图交换协议**不变：链编辑 = snapshot → rebuild → 双缓冲原子交换（复用 EngineHost 已有 API，新增 `insertModuleToChain` 一个入口）。
- **壳侧概念壳侧实现**：undo/redo、A/B 在宿主（EXE 形态）与前端 store（Demo 形态）各自实现，引擎不背。
- core 音频路径零第三方依赖红线不触及：HTTP 服务器只用 `std`；nlohmann/json 仅命令解析路径（与预设加载同属豁免区）。

## 3. 布局（信息层级）

```
┌ TitleBar 34px ── NAMFX · 工作上下文 · 拖动区 · 连接点 · [—][□][×] ──────┐
├ AppBar 52px ── 当前预设 | A/B | 撤销 | 调音器 | 总旁路 | 静音 | 电平 | 性能 ┤
├ ScenesStrip 38px ── [1][2]…[8] 场景 + 命名 + 存储 + MIDI 学习 ───────────┤
├──────────┬──────────────────────────────────────────────────────────────┤
│ 预设索引  │ 效果链画布：IN ↘ 01..06 ↘ 折返 ↘ 07..12 ↘ OUT                  │
│ 01A …     ├──────────────────────────────────────────────────────────────┤
│ 01B …     │ 工作台 tab：编辑 | 演出 | 模型库 | 控制源 | 系统              │
│ 01C …     │   编辑 = 分类 rail │ 模块目录 │ 参数工作台（顶栏固定操作）  │
│ 02A …     ├──────────────────────────────────────────────────────────────┤
├──────────┴──────────────────────────────────────────────────────────────┤
└ StatusBar 24px ── 状态消息 · xrun · 引擎版本/性能档位 ───────────────────┘
```

- **一级视图：预设侧边栏（最左）**——"我选哪个音色"。预设按 **ABC 三预设一组**编号：`01A / 01B / 01C / 02A …`（组号 = 顺序 1,2,3…；字母 = 组内 A/B/C）。组头显示 `01`、`02`…；条目显示 `01A · Clean`。编号来源：文件名若带 `NN[A-C]` 前缀则采用（如 `01A_clean.json`），否则按排序位置自动编号。
- **二级视图：效果链（右上）**——"这个音色怎么组出来的"。画布固定 12 个视觉槽位：上排 01-06，下排 07-12，线路右侧下折后向左再从左侧下折。模块卡片与图标显示类别、名称、资产名和旁路 LED；拖到已有模块交换，拖到空槽保留空洞布局。双击模块格旁路/启用，点击模块格打开参数。
- **三级视图：参数编辑（右下，编辑 / EDIT tab）**——"具体拧哪个旋钮"。三栏：
  1. **分类栏**（最左，窄）：过载/失真、压缩、调制、延迟、混响、滤波EQ、门限、音高、箱头、箱体IR、工具。
  2. **模块列表栏**：该分类下的模块（名称 + 简介 + 资产需求徽标）。资产型模块（箱头/箱体）点选后参数面板显示资产选择器。
  3. **参数面板**（其余空间）：**顶栏 = 模块名 + 旁路开关 + 干湿 + 上移/下移/删除**；下面为参数旋钮/滑杆网格（Helix Edit 风格：旋钮 + 数值 + 单位 + 双击重置 + 右键菜单 + MIDI 学习按钮）。
- **其余功能落位**（PLAN §10 要求）：
  - 场景条：链上方薄条（1-8，Helix 脚控排式），差异角标；`Alt+1..8` 切换（浏览器 Ctrl+1..8 被占用）。
  - A/B + undo/redo：AppBar（快捷键 1/2、Ctrl+Z/Y）。
  - 调音器：全屏覆盖层（快捷键 T），指针刻度 + 音名 + 弦匹配 + 无信号态。
  - 演出层（输出面板）：下半区「演出」tab（Master/InGain/三段 EQ/Low Cut/High Cut/静音/电平表/总旁路）。
  - 模型库：「模型库」tab（.nam 分类 Amp/Amp+Cab/Pedal + 品牌；IR 列表；导入上传）。
  - MIDI：「MIDI」tab（绑定列表 + 清空；参数/场景上的"学习"按钮进入 arm 态）。
  - 设置：「设置」tab（EXE 形态的音频设备/采样率/缓冲；版本/关于）。
  - 性能仪表：AppBar 常驻细条（绿/黄/红剩余性能）+ 悬停明细（每模块耗时，v2）。
  - 演出锁定视图：AppBar 右侧「锁定」开关 → 隐藏编辑 chrome、放大预设/场景名（v2 细化）。
  - 状态/错误：StatusBar 消息区 + 槽位内联错误（红色标记 + 定位/替换，不弹打断式弹窗）。

## 4. 视觉设计

### 4.1 设计令牌（单源 `webui/design-tokens.json` → CSS 变量）

| 令牌 | 值 | 用途 |
|---|---|---|
| `bg0` | `#070a0e` | 窗口底与信号流画布 |
| `bg1` | `#0b1016` | 面板底 |
| `bg2` | `#101821` | 抬升面板与参数顶栏 |
| `bg3` | `#16222d` | 悬停/表头 |
| `line` | `#202d39` | 细分隔线 |
| `line-strong` | `#30404e` | 面板描边 |
| `amber` | `#e7a34b` | **主强调色**（焦点/选中/插入） |
| `amber-hi` | `#ffd083` | 强调悬停/读数 |
| `led` | `#8bd3a3` | 活动 LED/剩余性能 |
| `blue` | `#7bb8d6` | IR/箱体资产语义 |
| `red` | `#e16b69` | 危险/错误/过载 |
| `text` | `#dbe4eb` | 主文本 |
| `text-dim` | `#81909d` | 次要文本 |
| `text-bright` | `#f6f8fa` | 强调文本 |
| `knob` | `#243441` | 旋钮体 |
| `knob-arc` | `#0a0e13` | 旋钮刻度槽 |

- 中文 UI + 英文行业术语（参数/模块名保留英文：Compressor、Gain、dB）；类别名中英并列（`过载 / Distortion`）。
- 语义色：**琥珀 = 焦点/主操作**（选中、进行中）；**绿 LED = 活动/启用**；**红 = 错误/过载/静音警示**。

### 4.2 字体（自包含，EXE 离线可用）

- 显示/数值字：**Rajdhani**（500/600/700，woff2 拉丁子集）——等宽感科技字，模块名、数值、刻度。
- 界面字：**Inter**（400/500/600/700）+ 中文系统回退（PingFang SC / Microsoft YaHei / Noto Sans CJK）。
- 无 CDN、无外部请求；字体文件随 `www/` 分发。

### 4.3 签名元素

**端口与信号航道**：效果链画布使用细网格、输入/输出端口和短连接线；启用时连接线上有低速琥珀流动，旁路时只保留暗线。预设索引使用连续编号，参数区用静态硬件旋钮承载细调。其余界面保持安静、高密度、细描边、小圆角、克制阴影。

### 4.4 旋钮

SVG 绘制硬件旋钮（圆形 + 指示线 + 刻度弧），拖拽垂直/水平调节、滚轮调节、双击输入数值、Shift 细调、右键菜单（重置默认 / 随机微调 / MIDI 学习）。`taper`（Linear/Log）与 min/max/unit 全部取自引擎注册表（`ParamSpec`）。

## 5. 预设编号（ABC 分组）

- 侧边栏预设列表（demo + 用户）统一编号：排序后按 `⌊i/3⌋+1` 组号、`i%3 → A/B/C` 字母；文件名为 `NNX_` 前缀时直接采用。
- 组头（`01`…）可点击折叠；当前预设高亮琥珀；demo 与用户预设分区（`出厂` / `我的`），编号连续不打断。
- 每组三格 A/B/C 排布：格内 = 编号 + 预设名 + 资产类型徽标（全 DSP / NAM / IR）。

## 6. 模块目录（引擎注册表 → UI）

| 分类（UI） | 模块 | 引擎类别 | 资产 |
|---|---|---|---|
| 过载/失真 Distortion | `od.ts808`（TS808 过载）`od.transparent`（Klon 透明过载）`od.mosfet`（OCD 失真） | pedal | — |
| 压缩 Compressor | `comp.ota`（Dyna Comp） | pedal | — |
| 调制 Modulation | `mod.chorus`（CE-2 合唱）`mod.flanger`（BF-2 镶边）`mod.phaser`（Phase 90）`mod.wah`（Crybaby 哇音） | pedal | — |
| 延迟 Delay | `dly.dm2`（DM-2 模拟延迟）`dly.tape`（Echoplex 磁带延迟） | pedal | — |
| 混响 Reverb | `rvb.spring`（弹簧混响）`rvb.hall`（Hall 混响） | pedal | — |
| 滤波/EQ Filter | `eq.ge7`（GE-7 图示均衡）`tone`（音色） | pedal | — |
| 门限 Gate | `gate.ns2`（NS-2 门限） | pedal | — |
| 音高 Pitch | `pitch.shift`（移调）`pitch.octave`（八度） | pedal | — |
| 箱头 Amp | `amp.nam`（NAM 箱头） | amp | .nam 模型 |
| 箱体 Cab/IR | `cab.ir`（IR 箱体） | cab | .wav IR |
| 工具 Utility | `gain`（增益） | pedal | — |

参数规格（min/max/默认/单位/taper）与引擎 `ParamSpec` 一一对应（前端 `catalog.js` 手工镜像，服务端模式以 `/api/state` 为准）。

## 7. 传输协议（宿主形态）

- `GET /` → `www/index.html`；`GET /app/...` → 静态文件；`GET /presets/...` → demo 预设 JSON（浏览器 demo 形态共用同一目录）。
- **模块目录单源 = 前端 `catalog.js`**（镜像引擎注册表）：宿主 `/api/state` 不重复下发 catalog（`catalog: null`），前端在收到宿主快照时用 `normalizeState()` 补齐链条目的中文名/类别与纯 UI 态。避免 C++ 侧重复维护 20 模块 × 参数规格。
- `GET /api/state` → 全量快照：presets(demo+user, 含 ABC label) / currentPreset / chain(specs+params) / chainLayout(12 槽模块 ID/空槽) / scenes / activeScene / dirty / output / levels / tuner / perf / midi / ab / undo / library / engine。
- `POST /api/cmd` body JSON `{"cmd":"...","...":...}`：
  - 预设：`loadPreset` `savePreset`（含 `chainJson` 导入）`deletePreset` `exportPreset`（返回 JSON 文本）
  - 链：`addModule`（链尾）`insertModule`（指定槽位）`removeModule` `moveModule` `moveModuleTo` `swapModule` `setParam` `setBypass` `setMix`
  - 场景：`recallScene` `saveScene`
  - 输出：`setOutput {key: master|ingain|bass|mid|treble|lowcut|highcut, value}`（数值）+ `setOutput {key: mute|masterBypass, value}`（布尔）
  - 音频设备：`setAudio {type, device, sampleRate, blockSize}`；原生宿主返回设备类型、设备列表和当前连接状态。
  - 全局：`setTunerOn` `setTuning`、`learnParam`/`learnScene`（宿主侧武装学习，WinMM MIDI 线程收到 CC 后自动完成绑定；调试/演示可发 `midiLearnParam{cc}` 直接完成）、`midiClear`、`undo` `redo` `copyToA/B` `applyA/B`
  - 响应 `{"ok":true}` / `{"ok":false,"error":"..."}`；变更后自动推送新快照（SSE）。
- `GET /api/events`（SSE）：`{"state":...}` 全量快照（离散变更后推送）与 `{"tick":true,"state":{levels,tuner,perf}}`（10Hz 心跳，前端走 applyTick 不重建 DOM）。SSE 单向事件 + POST 命令，规避 WebSocket 握手/分帧复杂度；`EventSource` 自动重连天然支撑 CONNECTING/RECONNECTING/STALE 三态。
- `PUT /api/import?name=xxx.nam`（raw body，Content-Length）→ 导入模型/IR 到用户库（Documents/namfx/models|irs）。
- Demo 形态：前端 `MockEngine` 实现同一语义（无网络，`?demo=1` 强制，宿主不可达自动回退）。

## 8. 窗口与 EXE 桥（WebView2 壳）

- 前端标题栏设计为窗口 chrome 预留：`-webkit-app-region: drag` 拖动区，右侧 `[—][□][✕]` 调用 `window.namfxBridge.windowControl('min|max|close')`（浏览器形态隐藏）。
- 当前壳形态使用系统标题栏（`setUsingNativeTitleBar(true)`），`window.namfxBridge` 桥为后续无边框窗口预留；壳功能（设备/采样率/缓冲）走后续 JUCE 设备面板。
- 窗口尺寸记忆、全屏演出视图、最小化到系统托盘（v2）均为壳职责，前端只发请求。

## 9. 音频后端

- 独立宿主 `namfx_web` = 原生 JUCE 音频后端（优先 ASIO，支持 WASAPI/DirectSound）+ HTTP 控制面；无可用设备时才保留控制面模式。
- **WebView2 壳**（正式形态）= 原生音频：`EngineAudioSource` + JUCE `AudioDeviceManager`（WASAPI 独占/共享 + ASIO），壳持有 EngineHost 并注入 WebHost；壳形态**不启动**音频泵（避免双 process 并发）。
- **浏览器 WASM** = AudioWorklet 驱动同一 core（见 §10）。
- 现场状态记忆（变更 debounce 2-5s 原子写盘）与备份（5 版）为壳职责，后续接入（复用 `core/preset/atomic_write` / `backup`）。

## 10. 浏览器 WASM 调试模式

- `?wasm=1` 不启动 C++ HTTP 宿主；浏览器主线程负责 WebUI 状态，`AudioWorklet` 负责实时音频，`namfx_wasm` 只暴露纯 C ABI。
- 编译范围：`Chain + ModuleRegistry + 内置 DSP/WDF + OutputStage + IR（wav_io/cab_ir）+ NAM（nam_amp，C++20 独立静态库，whole-archive 保静态架构注册）`；预设和参数命令沿用 WebUI 语义。
- **资产走内存加载**：core 新增 `ModuleBase::loadAssetBytes()`，Chain 支持自定义资产注入；NAM 用 `nam::get_dsp(json)`（`.nam` 即 JSON），IR 用 `ir::parseWav(bytes)`。JS 通过 `namfx_wasm_register_asset(name, bytes)` 注册，`www/models/`、`www/irs/` 为浏览器内置 demo 资产。
- 麦克风权限只由用户点击「启动音频」触发；浏览器必须运行在 `localhost` 或 HTTPS，实际采样率读取 `AudioContext.sampleRate`，不假设固定 48kHz。
- 音频回调内只使用预分配 WASM 缓冲区；JSON、预设解析、模型加载、WAV 解析和图构造全部在音频处理之外完成。
- WASM 构建产物写入 `www/wasm/`，开发时只需重新构建 `namfx_wasm` 并刷新页面，不需要打包 EXE。
- 延迟量级 10-25ms（AudioWorklet + 设备缓冲），定位为接琴调试；正式演出用壳形态。

## 11. WebView2 壳（正式形态）

- `desktop/App/webview_app.cpp` + `desktop/CMakeLists.txt` 的 `namfx_webview` 目标：JUCE `WebBrowserComponent`（WebView2 后端，`JUCE_USE_WIN_WEBVIEW2=1`）嵌入 `http://127.0.0.1:8812/?wasm=0`。
- 音频：与桌面编辑器相同的 `EngineAudioSource` + `AudioDeviceManager`（WASAPI 独占/共享 + ASIO，128 缓冲起步），`EngineHost` 由壳持有并注入 `WebHost`。
- 控制面：`web_host.cpp` 的 `WebHost` 支持外部 `EngineHost*`（`webHostCreate`/`makeHandler`/`NAMFX_WEB_EMBEDDED` 关闭独立 main），`HttpServer` 在后台线程运行；壳形态不启动音频泵（真实回调驱动）。
- 可移植路径：exe 旁 `www/` 与 `presets-demo/` 优先于编译期绝对路径（`packagedDir()`）。
- 打包：`tools/package-webui.ps1` → `build/namfx-webui-release/`（exe + WebView2Loader.dll + www + presets-demo + README）。
- WebView2 头文件与 `WebView2Loader.dll` 来自 NuGet `Microsoft.Web.WebView2`（本机解压于 `%TEMP%\opencode\webview2-sdk`，CMake 变量 `WEBVIEW2_SDK_DIR`）。

## 12. 文件布局与验收

```
webui/
├─ README.md              验收指引（快速开始 / 界面地图 / 验收清单 / 已知边界）
├─ DESIGN.md              本文件
├─ design-tokens.json     设计令牌单源（→ CSS 变量）
├─ CMakeLists.txt         宿主构建（option NAMFX_BUILD_WEBUI）
├─ server/
│  ├─ http_server.h       极简 HTTP/1.1 服务器（仅 std）
│  ├─ audio_backend.h     原生音频后端接口（无 JUCE 依赖）
│  └─ web_host.cpp        WebHost（EngineHost 桥 + 命令/事件 + 扫描 + undo/A-B + 泵）
│                         + makeHandler + 独立 main（NAMFX_WEB_EMBEDDED 可关闭）
├─ wasm/
│  ├─ CMakeLists.txt      Emscripten 目标 namfx_wasm（option NAMFX_BUILD_WASM）
│  ├─ namfx_wasm.h/.cpp   纯 C ABI 适配层
│  ├─ namfx_wasm_pre.js   预置脚本（Worklet 环境垫片）
│  └─ namfx_wasm_extern_pre.js
└─ www/                   静态前端（无构建步骤）
   ├─ index.html
   ├─ css/app.css
   ├─ js/{catalog,engine,wasm_engine,namfx_worklet,ui,main}.js
   ├─ fonts/*.woff2       自包含字体
   ├─ presets/*.json      demo 预设副本（浏览器 demo 形态）
   ├─ models/  irs/       浏览器内置 demo 资产（n-buna.nam / cab_clean.wav）
   └─ wasm/               namfx_wasm.js/.wasm（构建产物，勿手改）
```

**验收口径（用户确认）**：WebUI 是产品 GUI。优先浏览器可用性走查（Demo/WASM），
WebView2 壳（`build/namfx-webui-release/NAMFX.exe`）为正式交付形态；`namfx_web`
独立宿主保留为无壳调试入口。

**已知边界**：单实例模块约定（引擎控制路由按模块 ID 寻址）、undo 粒度 =
预设/拓扑/场景（参数微调不入栈）、MIDI 输入仅 Windows（WinMM）、浏览器 WASM
延迟高于原生后端；视觉空槽布局属于壳侧状态，导出预设仍保存连续音频链。
