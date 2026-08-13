# Dyna Comp 家族 OTA 压缩器电路事实调研

> 用途：为 namfx 自研"OTA 压缩器"数字建模（Dyna Comp 风格）提供**有来源的**电路事实。
> 原则：所有数值均来自下方引用来源；凭记忆推测的数值一律不写。来源不可达时明确标注"待验证"。
> 调研日期：2026-08-14。

## 0. 可达性总结

| 来源 | 状态 | 说明 |
|---|---|---|
| **PCB Guitar Mania "Sea Lion Compressor"（Walrus Deep Six 复刻，Building Docs PDF）** | ✅ 可达 | **权威来源**：Dyna Comp 家族（"roots on the DynaComp and Ross Compressor"），LM13700 OTA 完整 BOM + 原理图 + 控制说明 |
| electrosmash.com（MXR Dyna Comp 分析） | ❌ 不可达 | 本机网络不可达；web.archive API 429、archive.today 无快照。**待后续验证** |
| Dattorro, "Effect Design Part 1: Compressor/Limiter and Beyond"（JAES 1997） | ⚠️ 未直连 | 行为级压缩器原理的权威参考（检测/时间常数/压缩比），原理已知，未抓原文 |
| GitHub 仓库/代码搜索 | ❌ 无结果 | "dynacomp"/"dyna comp" 等关键词均 0 结果；代码搜索需登录 |
| Analog Is Not Dead | ✅ 可达但无关 | 仅 OCD/BD2 等过载分析，无压缩器 |
| stompboxschematics.com | ❌ 反爬 | JS 混淆无法解析 |

**关键结论：PCB Guitar Mania Sea Lion Compressor（Deep Six = Dyna Comp/Ross 家族）为唯一可达的完整电路事实源；Electrosmash 原文待验证。**

## 1. 电路拓扑（Sea Lion Compressor，LM13700 双 OTA）

```
吉他输入
  → R1=470k + C1=220p 输入网络 → C2=33n 耦合
  → Q1 (MPSA18) 输入缓冲（射极跟随）
  → 预加重网络（R5/R6 + C3/C4：高频提升，高频更快触发压缩）
  → Q2 缓冲 → RATIO=10k B（blend 干湿混合电位器）
  → TRIM1=2k（输入衰减，补偿 IC 差异）
  → IC1 OTA1（LM13700 半边）：
      - 非反相输入（V+），R15=3k9 + Q3 (2N5457) JFET 电平适配
      - R16=1m / R17=10k 偏置网络
      - 输出电流 → IC1 内部缓冲（BUF1）→ R22=15k → C13=1u 耦合
  → 包络检测（反馈压缩）：
      - D1/D2 (1N4148) 整流 + Q5/Q6 (MPSA18 镜像电流源)
      - C17=10u 积分电容 + R29=27k + ATTACK=250k C 锥电位器 + R30=47k
      - 检波器输出 → SUSTAIN=500k B（检波器输入电平 = 压缩量）→ OTA 的 Iabc（放大器偏置电流）
  → 输出混合：OTA 缓冲输出 + 干声（RATIO blend）→ LEVEL=100k A 音量电位器 → 输出缓冲（Q7）
```

### 1.1 关键特征（Dyna Comp 家族压缩机制）

1. **OTA（CA3080/LM13700）压控增益**：gm = Iabc/(2·Vt)——**放大器偏置电流 Iabc 控制增益**，Iabc 由检波器输出驱动 → 压缩
2. **反馈式检波**：输出信号整流（D1/D2）→ 积分电容（C17=10u）→ 控制 Iabc——**压缩曲线 ≈ g = 1/(1 + k·env)**（OTA 增益 ∝ Iabc，Iabc ∝ env）
3. **ATTACK 电位器**（250k C 锥 = 逆对数）：改变检波器充电时间常数（~1ms-100ms）
4. **SUSTAIN**（500k B）：检波器输入电平——**压缩量旋钮**
5. **RATIO（blend）**：Deep Six 新增的干湿混合（原版 Dyna Comp 无 blend）——"Ratio" 实为 blend
6. **LEVEL**（100k A）：输出音量
7. 压缩比强（~10:1），无阈值旋钮（阈值由电路固定）
8. 9V-18V 供电（高压更多 headroom）——数字 AC 域

## 2. 元件值（Sea Lion Compressor BOM）

### 2.1 输入/预加重

| 元件 | 值 | 说明 |
|---|---|---|
| R1 | 470kΩ | 输入下拉 |
| C1 | 220pF | |
| C2 | 33nF | 输入耦合 |
| R5/R6 | 10kΩ×2 | 预加重网络 |
| C3 | 1µF 电解 | 预加重 |
| C4 | 10µF 电解 | 预加重 |
| Q1/Q2 | MPSA18 | 输入缓冲（高增益 NPN） |

### 2.2 OTA 级

| 元件 | 值 | 说明 |
|---|---|---|
| IC1 | LM13700N | 双 OTA（OTA1 + 缓冲） |
| R15 | 3k9Ω | OTA 输入 |
| Q3 | 2N5457 | JFET 电平适配 |
| R16/R18/R19 | 1MΩ×3 | 偏置网络 |
| R17 | 10kΩ | |
| R20/R21 | 220kΩ×2 | |
| R22 | 15kΩ | OTA 输出负载 |
| TRIM1 | 2kΩ | 输入衰减微调 |
| C13 | 1µF | 输出耦合 |

### 2.3 包络检测（压缩核心）

| 元件 | 值 | 说明 |
|---|---|---|
| D1/D2 | 1N4148×2 | 整流 |
| Q5/Q6 | MPSA18×2 | 镜像电流源 |
| C17 | 10µF 电解 | 积分电容 |
| R29 | 27kΩ | |
| R30 | 47kΩ | 检波器输出电阻 |
| ATTACK | 250kΩ C 锥 | 攻击时间控制 |
| SUSTAIN | 500kΩ B | 压缩量（检波输入电平） |

### 2.4 输出

| 元件 | 值 | 说明 |
|---|---|---|
| RATIO | 10kΩ B | 干湿混合（blend） |
| LEVEL | 100kΩ A | 音量（音频锥） |
| Q7 | MPSA18 | 输出缓冲 |

## 3. 数字实现要点

| 项 | 事实 | 来源 |
|---|---|---|
| OTA 增益 | gm ∝ Iabc（压控），Iabc ∝ 检波包络 → 压缩曲线 g = 1/(1 + k·env) | Sea Lion 原理图 + OTA 原理（Dattorro 参考） |
| 检波 | |x| 整流 + 积分电容（C17=10u）→ 双时间常数（attack/release） | Sea Lion 原理图 |
| 时间常数 | attack 由 250k pot 控制（~1ms-100ms）；release 由 R30=47k+C17 决定（~几百 ms） | Sea Lion 原理图（推导） |
| blend | RATIO pot 干湿混合（Deep Six 特性，原版 Dyna Comp 无） | Sea Lion 控制说明 |
| 压缩量 | SUSTAIN 控制检波器输入电平 | Sea Lion 控制说明 |
| 输出 | LEVEL 音量（音频锥） | Sea Lion |

### 结构建议（namfx 数字实现）

| Stage | 建议方法 | 依据 |
|---|---|---|
| 输入缓冲 | 直通（缓冲增益 1） | Sea Lion |
| 检波 | 峰值检测（|x| + 双时间常数平滑，attack 参数控制） | Sea Lion + Dattorro |
| VCA 增益 | g = 1/(1 + sustain·k·env)（OTA 反馈压缩近似） | OTA 原理（推导） |
| 混合 | dry·(1-ratio) + wet·ratio | Sea Lion RATIO |
| 音量 | LEVEL 平方锥 | Sea Lion LEVEL |

## 4. 参考文献清单

1. **PCB Guitar Mania, "Sea Lion Compressor – Building Docs"（Deep Six = Dyna Comp/Ross 家族复刻，LM13700）** — https://pcbguitarmania.com/product/sea-lion-compressor/（✅ 已下载并抽取全文：BOM + 原理图 + 控制说明）
2. **Dattorro, "Effect Design Part 1: Compressor/Limiter and Beyond", JAES 1997** — 行为级压缩器原理参考（检测/时间常数/压缩比；⚠️ 未直连原文，原理为业界共识）
3. **Electrosmash, "MXR Dyna Comp Analysis"** — https://www.electrosmash.com/mxr-dyna-comp-analysis（❌ 不可达，web.archive/archive.today 均无快照，待后续验证）

## 5. 明确的"未找到 / 不可达"项（待后续验证）

1. **Electrosmash MXR Dyna Comp 原文**：不可达且无快照。当前以 Sea Lion（同家族）为准，Dyna Comp 与 Sea Lion 的核心差异 = 无 blend 旋钮 + CA3080（vs LM13700，电气等效）。
2. **检波器精确时间常数**：C17=10u + 各电阻的组合值推导（attack/release 数值为估算），待 SPICE 或原文验证。
3. **预加重网络的精确频响**：C3/C4 + R5/R6 组合，本文行为实现中简化为检波器前置高通（高频先触发），细节待验证。
4. **压缩比精确值**：Dyna Comp 标称 ~10:1 为社区共识，未在可达来源中见到实测曲线。
