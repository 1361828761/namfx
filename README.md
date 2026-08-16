# NAMFX

智能电吉他综合效果器平台：PC（JUCE 独立应用 + VST3/AU 插件）与嵌入式设备（RK 系 Cortex-A Linux 主力档 / STM32H7 低端档）共享同一纯 C++17 音频引擎（core/），支持 NAM 模型（.nam）、IR 卷积（.wav）、DSP 算法三类音色模块。

## 文档地图

| 文件 | 作用 | 何时读 |
|---|---|---|
| `docs/EXECUTION.md` | 执行手册：环境、当前任务、工程红线、已知坑 | 开始干活前必读 |
| `docs/PLAN.md` | 项目计划：全部决策 + 理由 + 决策审计 | 需要"为什么"时 |
| `CONTEXT.md` | 术语规范 | 写代码/文档前查词 |
| `TODOS.md` | 推迟的工作 | 本里程碑完成后 |

## 构建

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

要求：CMake ≥ 3.24，C++17，MSVC 2019+（Windows）/ GCC 9+ 或 Clang（Linux/macOS）。本机 Windows 需在 VS 2026 Developer PowerShell 中执行（cmake 不在 PATH）。

## WebUI（产品 GUI）

WebUI 是产品界面，三种运行形态共用同一前端 `webui/www`：

| 形态 | 启动 | 音频 |
|---|---|---|
| 浏览器模拟 | `python -m http.server 8765` 后开 `/?demo=1` | 无（界面走查） |
| 浏览器 WASM 调试 | `/?wasm=1` + 点「启动音频」 | AudioWorklet + `core`/NAM/IR 编译的 WASM（接琴调试用） |
| WebView2 桌面壳 | `build/namfx-webui-release/NAMFX.exe` | 原生 WASAPI/ASIO（正式形态） |

浏览器 WASM 构建（需 Emscripten，先 `emsdk_env`）：

```bash
cmake --preset wasm-debug
cmake --build --preset wasm-debug --target namfx_wasm
```

桌面壳打包：

```powershell
powershell -ExecutionPolicy Bypass -File tools\package-webui.ps1
```

细节见 `webui/README.md` 与 `webui/DESIGN.md`。

### 当前 WebUI 交付范围

- WebView2 桌面壳与独立 `namfx_web` 宿主支持原生 WASAPI/ASIO 音频设备选择。
- 音色链固定 12 个视觉槽位，采用上下两排蛇形线路；已有模块拖放时交换，空槽可接收模块。
- WebUI 扫描项目资源目录 `modles/nam/` 与 `modles/ir/`，并在打包时复制到 EXE 旁的 `models/`、`irs/`。
- 演出输出包含 Master、输入增益、三段 EQ、全局低切/高切、静音和总旁路。
- Release 壳使用 `tools/package-webui.ps1` 生成到 `build/namfx-webui-release/`。

## 许可

**GPL-3.0**（整体开源，2026-08-13 决策 #111）。依赖：NAM Core（MIT）、nlohmann/json（MIT，预设加载路径）、WaveDigitalFilters（GPL-3.0，仅 core/modules/dsp/ 电路建模路径）。
