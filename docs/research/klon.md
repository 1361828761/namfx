# Klon Centaur 电路事实调研（透明过载）

> 用途：为 namfx 自研"透明过载"数字建模（Klon 风格）提供**有来源的**电路事实。
> 原则：所有数值均来自下方引用来源；凭记忆推测的数值一律不写。来源不可达时明确标注"待验证"。
> 调研日期：2026-08-14。

## 0. 可达性总结

| 来源 | 状态 | 说明 |
|---|---|---|
| **jatinchowdhury18/KlonCentaur**（ChowCentaur，GitHub） | ✅ 可达 | **权威来源**：WaveDigitalFilters 库作者 Jatin Chowdhury 的 Klon Centaur 数字建模项目（380★），BSD-3-Clause 许可，含完整 WDF 电路模型源码 + SPICE 仿真训练数据；本文全部元件值均取自其源码 |
| electrosmash.com/klon-centaur-analysis | ❌ 不可达 | 本机网络不可达（与 ts808 调研时一致）；本文事实以 ChowCentaur 为准 |
| arXiv 2009.02833 | ⚠️ 未直连 | ChowCentaur 技术论文（建模方法论述，非元件值来源，不阻塞） |
| geofex / freestompboxes | 未访问 | ChowCentaur 已覆盖全部所需元件值，无需补充 |

**关键结论：ChowCentaur（BSD-3）源码 = 单一权威事实来源，全部元件值与拓扑可直接引用。**

## 1. 信号链拓扑（ChowCentaur 传统电路模型，GainStageProc.cpp + Plugin.cpp）

```
吉他输入
  → ×0.5（输入归一化）
  → 输入缓冲（一阶高通 IIR：R1=10k, R2=1M, C1=0.1µF，转折 ≈1.6Hz）
  → 运放摆幅钳位 clip(±4.5V)
  → PreAmp WDF（偏置网络 + 高通；Vbias = gain×100k）
      ├─ 主输出（电压）
      └─ FF1 侧链（current(Vbias2)，电流）
  → AmpStage（二阶 IIR：C7=82nF, C8=390pF, R11=15k, R12=422k, R10b=(1-gain)·100k+2k）
  → clip(±4.5V)
  → [2× 过采样] → ClippingWDF（二极管对 Is=15µA, Vt=25.85mV）→ [下采样]
  → FF2 WDF（前馈网络 2，RVTop=gain·100k, RVBot=(1-gain)·100k）
  → 求和：clipOut + ff1 + ff2
  → SummingAmp（一阶 IIR：R20=392k, C13=820pF）
  → clip(-13.1V, +11.7V)（不对称摆幅）
  → Tone（一阶 IIR：Rpot=10k, C=3.9nF, G1=1/100k, G2=1/(1.8k+(1-t)·10k), G3=1/(4.7k+t·10k), G4=1/100k）
  → ×-1（反相）
  → clip(-13.1V, +11.7V)
  → 输出级（一阶 IIR：R1=560+(1-l)·10k, R2=l·10k+1, C1=4.7µF，level 电位器）
  → DC blocker（35Hz 高通）
  → 输出
```

### 1.1 关键特征（Klon"透明"的机制）

1. **二极管饱和电流 Is = 15µA**（vs TS808 的 1N914 Is≈25nA，大 600 倍）→ 二极管拐点电压更高（~0.7V 处才开始强导通）且导通曲线更缓 → 低/中电平信号几乎不被削波，只有大动态才软限幅 → "透明过载"
2. **削波信号与两个前馈（干声）路径在求和运放中混合**：clipOut + ff1（PreAmp 电流侧链）+ ff2（FF2 网络）——干湿混合是电路固有的，非数字 mix 参数
3. **双 gain 联动**：gain 同时控制 PreAmp Vbias（偏置电阻）与 FF2 电位器（RVTop/RVBot 互补）+ AmpStage R10b——增益与音色联动
4. **输出不对称钳位** ±13.1V/+11.7V（运放摆幅不对称是失真音色的一部分）
5. 参数仅三个：**Gain / Treble / Level**（0..1，默认 0.5），无独立 Drive 旋钮

## 2. 各级元件值（全部取自 ChowCentaur 源码）

### 2.1 输入缓冲（InputBufferProcessor.cpp，一阶高通 IIR）

| 元件 | 值 |
|---|---|
| R1 | 10kΩ |
| R2 | 1MΩ |
| C1 | 0.1µF |

模拟传递：`bs=[C1·R2, 0] / as=[C1·(R1+R2), 1]`，K=2·fs 直接 BLT（一阶）。转折 ≈1.6Hz（直流隔离）。

### 2.2 PreAmp WDF（PreAmpStage.h/.cpp）

| 元件 | 值 | 说明 |
|---|---|---|
| C3 | 0.1µF | 输入耦合 |
| C5 | 68nF | 高通网络 |
| C16 | 1µF | 偏置去耦 |
| R6 | 10kΩ | |
| R7 | 1.5kΩ | |
| Vbias2 | 15kΩ（R）| FF1 = current(Vbias2) 从这里取 |
| Vbias | gain×100kΩ | gain 0..1 → 0..100k |

WDF 拓扑：`Vin → I1(极性反相) → S3 = (P3 + C3)`，P3 = S1∥S2，S1 = (C5∥R6) + Vbias，S2 = (Vbias2∥C16) + R7。输出 = voltage(Vbias) + voltage(R6)（求和节点），FF1 = current(Vbias2)。

### 2.3 AmpStage（AmpStage.h，二阶 IIR）

| 元件 | 值 |
|---|---|
| C7 | 82nF |
| C8 | 390pF |
| R11 | 15kΩ |
| R12 | 422kΩ |
| R10b | (1-gain)·100k + 2k（2k..102k，0.05s 平滑） |

模拟系数：`as = [C7·C8·R10b·R11·R12, C7·R10b·R11 + C8·R12·(R10b+R11), R10b+R11]`，`bs = [as0, C7·R11·R12 + as1, R12 + as2]`；BLT 预翘曲（wc = sqrt(as2/as0)）。

### 2.4 ClippingWDF（ClippingStage.h/.cpp + DiodePair.h）

| 元件 | 值 | 说明 |
|---|---|---|
| C9 | 1µF | 反馈电容 |
| R13 | 1kΩ | |
| C10 | 1µF | 输出耦合 |
| Vbias | 47kΩ | 输出偏置 |
| **D23 二极管对** | **Is = 15µA，Vt = 25.85mV** | 双二极管反向并联，CustomDiodePairT |

WDF 拓扑：`D23(root) → P1 = S2∥S3`，S2 = (Vin→I1反相 + C9) + R13，S3 = C10 + Vbias。输出 = current(C10)。**该级在 2× 过采样域运行**（半带多相 IIR 上下采样）。

CustomDiodePairT 反射（eqn 18 形式 + 小信号 LUT 修正）：
```
u = logR_Is_overVt + λ·a/Vt + R_Is_overVt    (λ = sign(a))
b = a + 2λ·(R_Is - Vt·wrightOmega(u))
|u| > 0.5：wrightOmega(u) 用 Omega::omega4 近似
|u| ≤ 0.5：wrightOmega(u) 用 LUT（-1..1，2^18 点，构建期 wrightOmega 真值）
```
> 注：omega4 在 u→0 附近数值误差大，安静信号时产生可闻失真——这正是 Klon 必须"安静时透明"的敏感区，小信号必须走 LUT。

### 2.5 FeedForward2 WDF（FeedForward2.h/.cpp）

| 元件 | 值 | 说明 |
|---|---|---|
| R5 | 5.1kΩ | |
| R8 | 1.5kΩ | |
| R9 | 1kΩ | |
| RVTop | gain·100kΩ | 与 RVBot 互补 |
| RVBot | (1-gain)·100kΩ | |
| R15 | 22kΩ | |
| R16 | 47kΩ | |
| R17 | 27kΩ | |
| R18 | 12kΩ | |
| C4 | 68nF | |
| C6 | 390nF | |
| C11 | 2.2nF | |
| C12 | 27nF | |
| Vbias | 0V | 偏置（AC 域） |

WDF 拓扑：`Vin → I1(反相) → S7 = (P6∥R5,C4) + S6`，P6 = R5∥C4；S6 = P5 + Vbias，P5 = P4∥R8，P4 = S4∥S5，S5 = C6 + R9，S4 = P3 + RVTop，P3 = P2∥RVBot，P2 = S3∥P1，P1 = S1∥R17，S1 = C12 + R18，S3 = S2 + R16，S2 = C11 + R15。输出 = current(R16)。

### 2.6 SummingAmp（SummingAmp.h，一阶 IIR）

| 元件 | 值 |
|---|---|
| R20 | 392kΩ |
| C13 | 820pF |

模拟系数：`bs=[0, R20] / as=[C13·R20, 1]`，K=2·fs 直接 BLT。

### 2.7 Tone（ToneFilterProcessor.cpp，一阶 IIR）

| 元件 | 值 | 说明 |
|---|---|---|
| Rpot | 10kΩ | treble 电位器 |
| C | 3.9nF | |
| G1 | 1/100kΩ | |
| G2 | 1/(1.8k + (1-t)·10k) | |
| G3 | 1/(4.7k + t·10k) | |
| G4 | 1/100kΩ | |

模拟系数：`bs=[C·(G1+G2), G1·(G2+G3)] / as=[C·(G3-G4), -G4·(G2+G3)]`，wc=G1/C 预翘曲；BLT 后**极点翻转保证稳定**（a[1]=1/aU[1]）。

### 2.8 输出级（OutputStageProcessor.cpp，一阶 IIR）

| 元件 | 值 | 说明 |
|---|---|---|
| R1 | 560 + (1-l)·10kΩ | level 电位器上臂 |
| R2 | l·10k + 1Ω | 下臂（≥1Ω） |
| C1 | 4.7µF | 输出耦合 |

模拟系数：`bs=[C1·R2, 0] / as=[C1·(R1+R2), 1]`，K=2·fs 直接 BLT。

### 2.9 全局

- 电源：9V，4.5V 偏置（AC 域模型，Vbias 电压置 0）
- DC blocker：35Hz 一阶高通（插件输出端）
- 2× 过采样仅 ClippingWDF 级
- 电位器平滑：gain/treble/level 相关电阻 0.05s 平滑（AmpStage R10b / Tone / OutputStage）

## 3. 数字实现要点

| 项 | 事实 | 来源 |
|---|---|---|
| 过采样 | 仅削波级 2×（半带多相 IIR），PreAmp/AmpStage/FF2 基率 | GainStageProc.cpp |
| 输入缩放 | ×0.5（插件输入归一化，数字 1.0 ≈ 9V 电源域一半） | Plugin.cpp processInternalBuffer |
| 钳位 | 运放摆幅 ±4.5V（前级）/ -13.1V..+11.7V（求和与输出级，不对称） | 同上 |
| 二极管 | 反向并联对 Is=15µA、Vt=25.85mV；小信号 LUT wrightOmega | DiodePair.h |
| 干湿混合 | 电路固有：clipOut + ff1 + ff2 求和 | GainStageProc.cpp |
| 参数 | gain/treble/level 均 0..1 默认 0.5 | Plugin.cpp addParameters |

### 结构建议（namfx 数字实现）

| Stage | 建议方法 | 依据 |
|---|---|---|
| 输入缓冲 | 一阶 IIR（BLT） | InputBufferProcessor.cpp |
| PreAmp | WDF（chowdsp wdft，同拓扑） | PreAmpStage.h |
| AmpStage | 二阶 IIR（BLT 预翘曲） | AmpStage.h |
| Clipping | WDF（2× 过采样 + Is=15µA 二极管对 + 小信号 LUT） | ClippingStage.h |
| FF2 | WDF（同拓扑） | FeedForward2.h |
| SummingAmp | 一阶 IIR | SummingAmp.h |
| Tone | 一阶 IIR（极点翻转稳定处理） | ToneFilterProcessor.cpp |
| 输出级 | 一阶 IIR + level 分压 | OutputStageProcessor.cpp |

## 4. 参考文献清单

1. **jatinchowdhury18/KlonCentaur**（BSD-3-Clause）— https://github.com/jatinchowdhury18/KlonCentaur
   - `ChowCentaur/ChowCentaurPlugin.cpp`（信号链/参数/钳位）
   - `ChowCentaur/GainStageProcessors/{PreAmpStage, ClippingStage, FeedForward2, AmpStage, SummingAmp, DiodePair, GainStageProc}.{h,cpp}`（增益级全部元件值）
   - `ChowCentaur/CommonProcessors/{InputBufferProcessor, OutputStageProcessor, ToneFilterProcessor}.cpp`（输入/输出/音调级）
   - arXiv 论文：https://arxiv.org/abs/2009.02833
2. Stefano D'Angelo, "Wright Omega"（MIT）— https://www.dangelo.audio/dafx2019-omega.html（omega4 近似，经 ChowCentaur/WDF 库采用）
3. **Electrosmash, "Klon Centaur Analysis"** — https://www.electrosmash.com/klon-centaur-analysis（❌ 本机不可达，待后续验证；ChowCentaur 与其结论一致的可能性高，但本文不依赖）

## 5. 明确的"未找到 / 不可达"项（待后续验证）

1. **Electrosmash Klon 原文**：不可达。当前以 ChowCentaur（BSD-3，作者即 WDF 库作者，建模质量业界认可）为唯一事实源。
2. **物理机元件值**：ChowCentaur 元件值来自 SPICE 反推/公开拆解图（其 GainStageTraining/SPICESim.py），未与第三方拆解交叉验证；如后续获得 freestompboxes 拆解图可复核。
3. **偏置电压**：ChowCentaur 在 AC 域建模（Vbias 电压置 0）；如需 DC 域输出（含 4.5V 偏置）需另行处理——namfx 不需要（数字输出 AC 域）。
4. **Klon 的"魔法二极管"具体型号**：ChowCentaur 用 Is=15µA 拟合；物理机据说为 1N4148+红色 LED 组合（等效高拐点），具体型号未验证——不影响建模（Is 已拟合等效行为）。
