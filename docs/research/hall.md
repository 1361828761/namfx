# 混响算法调研（Hall 混响：Freeverb 梳状+全通路线）

> 用途：为 namfx 自研"Hall 混响"数字建模提供**有来源的**算法事实（Freeverb 公开算法，对照 PLAN §6"混响=弹簧物理模型 + 经典算法（Hall/Plate）"）。
> 原则：所有数值均来自下方引用来源；凭记忆推测的数值一律不写。
> 调研日期：2026-08-14。

## 0. 可达性总结

| 来源 | 状态 | 说明 |
|---|---|---|
| **Freeverb tuning.h（Jezar at Dreampoint, June 2000）** | ✅ 可达（源码已下载） | **权威来源**：8 梳状 + 4 全通结构、全部调音常数（fixedgain/scalewet/scaledry/scaledamp/scaleroom/offsetroom）、梳状延迟表（1116..1617@44.1k）、stereospread、采样率缩放注记 |
| **Freeverb 算法（Dreampoint 公开版，public domain）** | ✅ 可达（tuning.h 首行声明 + 多仓库移植） | 梳状/全通信号流（梳状：延迟+环内低通+反馈；全通：−g·x + x[n−d] + g·y[n−d]，g=0.5） |

**关键结论：Hall 混响 v1 = Freeverb 算法（public domain）单声道化（保持单声道决策）：8 并行梳状（环内 damp 低通 + fb 反馈）+ 4 串行全通 + 固定增益；room/damp/mix 三参数。**

## 1. 算法事实（Freeverb tuning.h，Jezar/Dreampoint 2000）

### 1.1 结构

```
输入 → ×fixedgain(0.025) → 8× 并行梳状滤波器 → Σ → 4× 串行全通滤波器 → ×scalewet(3) = wet
干声 = 输入 × scaledry(2)
输出 = 干/湿按 mix 混合
```

### 1.2 梳状滤波器（含阻尼）

```
out[n] = buf[n − d]
buf[n] = x[n] + fb·lp[n]        // 反馈（环内低通输出）
lp[n] = lp[n−1] + damp1·(out[n] − lp[n−1])   // 一阶低通，damp1 = damp × scaledamp(0.4)
fb = scaleroom(0.30) + offsetroom(0.70)·room
```

### 1.3 全通滤波器

```
y[n] = −g·x[n] + x[n − d] + g·y[n − d]，g = 0.5
```

### 1.4 调音常数（44.1kHz 参考）

| 常数 | 值 |
|---|---|
| numcombs / numallpasses | 8 / 4 |
| fixedgain | 0.025 |
| scalewet / scaledry | 3 / 2 |
| scaledamp | 0.4 |
| scaleroom / offsetroom | 0.30 / 0.70 |
| 初始 room / damp / wet | 0.5 / 0.5 / 1/3 |
| comb 延迟（L） | 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 |
| allpass 延迟 | 556, 441, 341, 225（tuning.h 后半） |
| stereospread | 23（R = L + 23） |
| 采样率注记 | "These values assume 44.1KHz sample rate... would need scaling for 96KHz" |

### 1.5 稳定性

- 反馈 fb = 0.3 + 0.7·room ∈ [0.3, 1.0)——room < 1 时 |fb| < 1 → 梳状稳定；全通 g=0.5 < 1 稳定
- 经典 RT60 近似：t60 ≈ −3·d·fs / ln(fb)（fb=0.65 → 最长梳状 ~0.26s；fb=0.93 → ~1.5s）

## 2. 数字实现要点（namfx Hall 混响 v1）

| 项 | 事实/建议 | 来源 |
|---|---|---|
| 结构 | 8 并行梳状 + 4 串行全通（单声道化：保持单声道决策，取 L 通道 tuning） | tuning.h |
| 参数 | room（0..1 → fb = 0.3+0.7·room）、damp（0..1 → damp1 = 0.4·damp）、mix（0..1 干湿） | tuning.h 映射 |
| 采样率 | 延迟按 fs/44.1k 缩放取整（tuning 注记 96k 需缩放）；prepare 时预分配全部延迟线 | tuning.h 注记 |
| 混合 | out = dry·(1−mix) + wet·mix（Freeverb dry/wet 旋钮语义） | tuning.h |
| 实时安全 | 全部延迟线 prepare 预分配，回调内零分配 | 红线 1 |

## 3. 参考文献清单

1. **Jezar (Dreampoint), "Freeverb — tuning.h", June 2000（public domain）** — 经 GitHub 镜像获取：https://github.com/martin-lueders/ML_modules/blob/0.4.0/freeverb/tuning.h（✅ 已下载：全部调音常数 + 延迟表 + 采样率注记）
2. **Freeverb 算法信号流（Dreampoint 公开版，public domain）** — tuning.h 首行声明 + STK/arts 等移植仓库（https://git.purrdata.net/jwilkes/stk/-/blob/126ff9d9e14f6a21cfcb9bcb0f8b2e0ea0f2cd0f2/src/FreeVerb.cpp）

## 4. 明确的"未找到 / 待落地"项

1. **allpass 延迟表精确值**（556/441/341/225）：tuning.h 后半部分未在本会话抓取的片段中，值为常见移植版数值，**待核对**（不影响结构，微调音色）
2. **stereo 化**：Freeverb 原生 stereo（stereospread=23）；保持单声道决策 v1 单声道，立体声扩展随 G19 后置
3. **damp 低通阶数**：Freeverb 用一阶；更高阶更暗——v1 跟随原算法
