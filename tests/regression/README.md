# tests/regression — 音频回归（黄金输出比对）

对应 PLAN §12：黄金输出比对标准 = 相对误差 + 频域特征双重断言（纯样本比较在 NAM 上必炸；DSP 白盒模块用确定性算法）。

## 当前用例

| 用例 | 场景 | 黄金文件 | 断言 |
|---|---|---|---|
| ts808 固定参数 | 48kHz / 2s / 220Hz 正弦 0.3 / Drive=5 Tone=5 Level=0dB | `data/ts808_drive5_tone5_level0.f32` | RMS 相对误差 < 1e-5（x86）/ 1e-3（ARM）+ 三次谐波能量比相对偏差 < 1% |

## 结构

- `golden_gen.cpp` — 黄金文件生成器（手动运行，**修改实现后必须重新生成并提交新黄金**）
- `golden_test.cpp` — 比对测试（CI 自动跑：`namfx_golden_tests`）
- `data/` — 黄金文件（raw float32 单声道，little-endian，提交入库）

## 新增用例流程

1. 在 `golden_gen.cpp` / `golden_test.cpp` 中成对添加场景（同一组常量：采样率/时长/输入/参数）
2. 运行生成器产出黄金文件
3. 运行测试确认通过
4. 提交黄金文件（无黄金文件时测试失败——黄金是断言基线，不是产物）

## 阈值分档

- x86：RMS 相对误差 1e-5（PLAN §12）
- ARM（`__aarch64__`）：1e-3（"不一致是特性不是 bug"；linux-arm64 runner 接入后生效，决策 #99）
- 频域特征：谐波能量比相对偏差 1%（Goertzel 于尾部 2s 窗口）

## 重新生成命令

```
cmake --build build/debug --target namfx_golden_gen --parallel 8
build/debug/tests/regression/Debug/namfx_golden_gen.exe
```
