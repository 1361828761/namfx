# 哇音效果器电路事实调研（Dunlop Cry Baby 风格，LC/状态变量谐振滤波）

> 用途：为 namfx 自研"哇音"数字建模（Dunlop Cry Baby GCB-95 风格）提供**有来源的**电路事实。
> 原则：所有数值均来自下方引用来源；凭记忆推测的数值一律不写。来源不可达时明确标注"待验证"。
> 调研日期：2026-08-14。

## 0. 可达性总结

| 来源 | 状态 | 说明 |
|---|---|---|
| **DAFx15 论文 "Digitizing the Ibanez Weeping Demon Wah Pedal"（C. Gnegy）** | ✅ 可达（PDF 已下载并全文抽取） | **权威来源**：哇音滤波 = 状态变量滤波器（SVF）四运放结构 + 传递函数推导 + "band+low 合并为谐振低通，哇音踏板常见"结论 |
| **Vectree "Circuitry and signal processing of the Cry Baby Wah-Wah pedal"** | ✅ 可达（页面已下载） | Cry Baby 拓扑确认：Q1 增益级 → LC 谐振滤波（电感 Q 决定峰锐度）→ 100k 踏板电位器（改变反馈 → 谐振频率移动）→ Q2 输出缓冲 |
| **ElectroSmash "Dunlop Crybaby GCB-95 Circuit Analysis"** | ❌ 不可达 | 本机网络不可达（与既往调研一致）。**待后续验证**：精确元件值 + 扫频范围数值 |

**关键结论：以 DAFx15 论文（同族电感哇音的完整 SVF 电路分析）为滤波结构依据，Vectree 确认 Cry Baby 拓扑；行为级建模用 SVF 谐振滤波，参数 position/resonance/level。**

## 1. 电路拓扑（Dunlop Cry Baby GCB-95）

```
吉他输入
  → 输入缓冲/增益级（Q1，2N5172 或 MPSA18 类高增益预放大；低输入阻抗与拾音器交互
     → 音色特征之一）
  → 谐振滤波（LC 谐振电路：电感（"Fasel" 磁环类）+ 电容；电感 Q 决定谐振峰锐度
     → "人声"感；磁芯饱和产生非线性）
  → 100k 电位器（踏板经齿轮组联动；不是音量控制，而是改变电路反馈
     → 谐振频率上下移动 = 扫频）
  → 输出缓冲（Q2，射极跟随，低输出阻抗）
```

### 1.1 关键特征（哇音音色机制）

1. **谐振峰扫频**：踏板移动 → 电位器阻值变化 → 谐振频率在频谱上下移动——"哇"声
2. **Q/谐振**：电感 Q（电路反馈）决定峰锐度——"人声/歌唱"感
3. **滤波类型**：DAFx15 明确指出"第四运放合并 band- 与 low-pass 输出 → 谐振低通，**哇音踏板常见**"（LC 谐振网络本质也是串联电感+并联电容的谐振低通）
4. **控制维度**：中心频率 + Q 两维（DAFx15："与 Dunlop Crybaby 相似，Weeping Demon 对滤波器中心频率与 Q 提供独立控制"）
5. **输入缓冲**：级联射极跟随，高通 ~4Hz，远在音频带外 → 数字建模忽略

## 2. 电路结构与元件（DAFx15，Weeping Demon；Cry Baby 精确值待验证）

### 2.1 SVF 结构（四运放）

| 运放 | 角色 | 说明 |
|---|---|---|
| A | 输入差分放大器（高/带通成形） | 非反相输入来自滤波输入与 C 输出；输出 A 与 B 恒为 180° 反相 |
| B | 第一积分器（低通） | RC 时间常数决定截止 |
| C | 第二积分器（低通） | 两次积分 = 二阶 |
| D | 输出差分放大器 | 合并 band（B）与 low（C）输出 → 谐振低通；增益由 R120 等决定 |

- Q 电位器（RQ）反馈从 B（带通）引回 A → 合理 Q 值（无反馈时 Q 极高/振荡）
- 模式开关：Bass/Normal 两档（改变 ZA/ZK 电容组合）；数字模型不建模（行为参数）
- 光学传感器（LED+光敏电阻+挡片）检测踏板角度 → 中心频率（论文建模重点；本模型直接用 position 参数）

### 2.2 数字实现路线（论文采用）

1. Mason 规则求各节点传递函数 → 双线性变换数字化（[bilinear transform](https://ccrma.stanford.edu/~jos/pasp/Bilinear_Transformation.html)）
2. 文献对比：拟合双二阶到实测响应（[CCRM Faust 哇音](https://ccrma.stanford.edu/realsimple/faust_strings/Adding_Wah_Pedal.html)）可抓全局行为；Holters/Zölzer 用 nodal DK 建模 Dunlop Crybaby；Falaise-Skrzek 用 port-Hamiltonian——本模型选**行为级 SVF**（PLAN §6 行为级复现路线）

## 3. 数字实现要点

| 项 | 事实 | 来源 |
|---|---|---|
| 滤波结构 | SVF（状态变量滤波器），谐振低通 = band+low 合并 | DAFx15 |
| 控制 | 中心频率（踏板位置）+ Q（谐振）独立控制 | DAFx15 |
| 扫频范围 | 精确数值**待验证**（electrosmash 不可达）；行为默认 fc ∈ [300Hz, 2500Hz] 对数扫频（常见哇音范围量级） | 待验证 |
| 缓冲 | 级联射极跟随 ~4Hz 高通，带外忽略 | DAFx15 |
| 增益 | Q 电位器反馈 → 谐振峰可超 0dB（负反馈减弱即提升） | DAFx15/Vectree |

### 结构建议（namfx 数字实现，行为级）

| Stage | 建议方法 | 依据 |
|---|---|---|
| 滤波级 | TPT（Zavalishin）状态变量滤波器，输出 = low + band 合并（谐振低通，峰值处 ~Q 提升）——DAFx15 明示哇音常见拓扑；带通输出为备选 | DAFx15 |
| 扫频 | position 0..1 → fc = 300·(2500/300)^position 对数扫频 | 行为参数（范围待验证） |
| 谐振 | resonance 0..1 → Q = 1 + 14·resonance | 行为参数（Q 独立控制事实来自 DAFx15） |
| 输出 | bandpass × level（level 作输出增益） | 行为参数惯例（对照 chorus/phaser） |
| 控制源 | position 参数即控制源预留钩子（M5 控制源抽象接入后由表情踏板/CC 驱动） | PLAN §5 控制源抽象 |

## 4. 参考文献清单

1. **C. Gnegy, "Digitizing the Ibanez Weeping Demon Wah Pedal", DAFx-15, Trondheim, 2015** — https://www.ntnu.edu/documents/1001201110/1266017954/DAFx-15_submission_73.pdf（✅ 已下载并全文抽取：SVF 结构 + 传递函数 + 谐振低通结论 + 光学传感器建模）
2. **Vectree, "Circuitry and signal processing of the Cry Baby Wah-Wah pedal"（Visual Explainer）** — https://vectree.io/c/guitar-signal-processing（✅ 已下载：Cry Baby 拓扑 + 100k 电位器机制 + Q1/Q2 角色）
3. **ElectroSmash, "Dunlop Crybaby GCB-95 Circuit Analysis"** — https://www.electrosmash.com/crybaby-gcb-95（❌ 不可达，**待后续验证**：精确元件值 + 扫频范围数值）
4. **CCRM, "Adding a Wah Pedal to Faust"（带通/谐振滤波数字实现惯例）** — https://ccrma.stanford.edu/realsimple/faust_strings/Adding_Wah_Pedal.html（未下载，行为级惯例参照）

## 5. 明确的"未找到 / 不可达"项（待后续验证）

1. **Cry Baby 精确元件值**（电感 mH 值、电容、100k 电位器锥度）：electrosmash 不可达；DAFx15 为 Weeping Demon 值
2. **扫频范围精确数值**：行为默认 fc ∈ [300Hz, 2500Hz]（对数），待 electrosmash 可达后核对
3. **LC 磁芯饱和非线性**（Vectree 提及"磁芯饱和增加非线性"）：v1 行为级不建模
4. **输入阻抗与拾音器交互**（Cry Baby 非真旁路时的音色加载）：v1 不建模（数字模型无负载概念）
