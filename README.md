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

要求：CMake ≥ 3.24，C++17，MSVC 2019+（Windows）/ GCC 9+ 或 Clang（Linux/macOS）。
