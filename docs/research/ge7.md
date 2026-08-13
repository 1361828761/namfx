# 图示均衡效果器电路事实调研（Boss GE-7 风格，7 段有源滤波）

> 用途：为 namfx 自研"图示均衡"数字建模（Boss GE-7 Graphic Equalizer 风格）提供**有来源的**电路事实。
> 原则：所有数值均来自下方引用来源；凭记忆推测的数值一律不写。来源不可达时明确标注"待验证"。
> 调研日期：2026-08-14。

## 0. 可达性总结

| 来源 | 状态 | 说明 |
|---|---|---|
| **Boss 官方产品页 "GE-7 Equalizer"** | ✅ 可达（页面已下载） | **权威来源**：7 段频点 + 每段 ±15dB + Level ±15dB + **滤波类型官方定义（前六段 peaking、6.4kHz 段 shelving）** + 规格 |
| **ElectroSmash "Boss GE-7 analysis"** | ❌ 不可达 | 本机网络不可达（与既往调研一致）。**待后续验证**：电路级元件值/滤波器 Q 值 |

**关键结论：以 Boss 官方产品页为行为基准（频点/增益范围/滤波类型全部官方确认）；滤波器 Q 为行为参数（待验证）。**

## 1. 电路事实（Boss 官方）

### 1.1 官方规格

| 项 | 值 | 说明 |
|---|---|---|
| 频段 | **100Hz / 200Hz / 400Hz / 800Hz / 1.6kHz / 3.2kHz / 6.4kHz** | 吉他优化八度间隔 7 段 |
| 每段增益范围 | **±15dB** | "Maximum tonal flexibility with ±15 dB boost/cut per band" |
| Level | **±15dB** | "balance the overall volume with the bypassed signal and use the pedal as a clean boost" |
| 滤波类型 | 前六段（100Hz-3.2kHz）= **peaking 滤波器**；6.4kHz 段 = **shelving 滤波器** | "The first six bands (100 Hz to 3.2 kHz) feature peaking filters... while the 6.4 kHz slider employs a shelving filter for smooth treble adjustments" |
| 规格 | 输入 1MΩ / 标称 -20dBu / 缓冲旁路 / 9V | 产品页规格表 |

### 1.2 关键特征

1. **图形 EQ 拓扑**：每频段独立滤波器，±15dB 提升/衰减——"从细微润色到激进塑形"
2. **前六段 peaking + 6.4kHz shelving**：官方明确的高频平滑处理（shelving 避免"刺耳"的峰值式高频调整）
3. **Level 独立控制**：与旁路信号平衡音量 / 作 clean boost

## 2. 数字实现要点

| 项 | 事实 | 来源 |
|---|---|---|
| 频点 | 100/200/400/800/1600/3200/6400 Hz（八度间隔） | Boss 官方 |
| 增益 | 每段 ±15dB；Level ±15dB | Boss 官方 |
| 滤波类型 | 段 0-5 = peaking；段 6（6.4k）= high shelving | Boss 官方 |
| Q/斜率 | 官方未给 → 行为参数：peaking Q = 1.1、shelf S = 1（**待验证**） | 待验证 |

### 结构建议（namfx 数字实现，行为级）

| Stage | 建议方法 | 依据 |
|---|---|---|
| 滤波级 | 7 个级联双二阶（RBJ）：段 0-5 peaking EQ（Q=1.1），段 6 high shelf（S=1）；零增益时各段恒等 → 级联平坦 | Boss 官方滤波类型 |
| 增益映射 | band 参数 0..1 → -15..+15dB；level 0..1 → -15..+15dB | Boss 官方 |
| 平滑 | 各段 dB 值 10ms 一阶平滑 → 逐样本重算系数（防 zipper） | 行为参数惯例（对照 chorus 等） |
| 结构 | DF1 双二阶，系数逐样本更新；w0/cos/alpha 在 prepare 预计算，逐样本仅 A=10^(dB/40) 变化 | 行为参数惯例 |

## 3. 参考文献清单

1. **Boss 官方, "GE-7 Equalizer — Features & Specs"** — https://www.boss.info/nz/products/ge-7/features/（✅ 已下载：7 段频点 + ±15dB + Level ±15dB + peaking/shelving 官方滤波类型 + 规格）
2. **ElectroSmash, "Boss GE-7 analysis"** — https://www.electrosmash.com/ge7（❌ 不可达，**待后续验证**：电路级元件值/滤波器 Q）

## 4. 明确的"未找到 / 不可达"项（待后续验证）

1. **滤波器 Q / shelf 斜率**：官方未给；行为默认 peaking Q=1.1、shelf S=1，待 electrosmash 可达后核对
2. **电路级实现**（有源滤波拓扑/元件值）：electrosmash 不可达；v1 行为级建模
3. **带外行为**：官方未给 <100Hz 与 >6.4kHz 的精确响应；行为模型 = 级联双二阶自然滚降
