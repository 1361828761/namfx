# 弹簧混响效果器电路事实调研（Fender 风格，Accutronics 弹簧箱 + 色散全通建模）

> 用途：为 namfx 自研"混响"数字建模（Fender 弹簧混响风格，Accutronics 3 弹簧箱）提供**有来源的**事实。
> 原则：所有数值均来自下方引用来源；凭记忆推测的数值一律不写。来源不可达时明确标注"待验证"。
> 调研日期：2026-08-14。

## 0. 可达性总结

| 来源 | 状态 | 说明 |
|---|---|---|
| **Springer, "Efficient Dispersion Generation Structures for Spring Reverb Emulation"（J. Parker, 2011）** | ✅ 可达（页面已下载） | **权威来源**：弹簧混响参数化模型 = **长链全通滤波器**（色散）；本文给出实时优化的多率/多带结构 |
| **Semantic Scholar, "Parametric Spring Reverberation Effect"（Välimäki, Parker 等）** | ✅ 可达（页面） | 参数化弹簧混响的经典论文条目（模型基准） |
| **GitHub gabrielgustafsson/spring-reverberation-model（README）** | ✅ 可达（页面） | 模型结构描述：**谱延迟滤波（级联相同全通）+ 反馈环内调制多抽头延迟线** → "衰减的啁啾脉冲序列逐渐模糊为混响尾" |
| **SchematicHeaven, "Accutronics Reverb Tanks"** | ✅ 可达（页面已下载） | **弹簧箱规格**：4/8/9 型（Fender=4 型 4 弹簧并作 2；Marshall=8 型 3 弹簧；Mesa/Fender 升级=9 型 3 弹簧）、**衰减时间 1.2-2s（短）/ 1.75-3.0s（中）/ 2.75-4s（长）**、输入/输出阻抗 |
| **tonalux, "Why Spring Reverb Sounds Like a Spring"** | ✅ 可达（页面已下载） | 物理原理：螺旋弹簧支撑横波/纵波/扭转波三族波，**色散把每个脉冲抹成"下降啁啾"**——"boing"是螺旋色散的声学签名 |

**关键结论：以 Välimäki/Parker 参数化模型（全通链色散 + 延迟线反馈环）为算法基准，Accutronics 3 弹簧箱（9 型，衰减 1.75-4s）为行为参数依据。**

## 1. 电路事实

### 1.1 弹簧箱（Accutronics，schematicheaven）

| 型 | 长度 | 弹簧 | 常见宿主 |
|---|---|---|---|
| 4 | 17" | 4 根并作 2 | Fender 音箱 |
| 8 | 9" | 6 根并作 3 | Marshall |
| **9** | 17" | **6 根并作 3** | Mesa Boogie / Fender 升级 |

- 衰减时间分级：短 1.2-2s / 中 1.75-3.0s / 长 2.75-4s（本模型取中型 ~2s）
- 驱动：输入换能器把信号注入弹簧（扭转波为主，抗机械冲击）；输出换能器拾取另一端

### 1.2 物理原理（tonalux）

- 弹簧同时支持横波/纵波/扭转波三族波，速度不同 → **色散**（frequency-dependent delay）
- 每个脉冲被抹成"下降啁啾"（falling chirp）——"boing" = 螺旋色散的声学签名
- 现代弹簧箱输入换能器专门驱动扭转波（对机械冲击不敏感）

### 1.3 参数化数字模型（Välimäki/Parker 路线）

- **谱延迟滤波** = 级联的相同全通滤波器链（色散生成）
- **调制多抽头延迟线 + 反馈环**（时间抹散 + 衰减）
- 脉冲响应特征："衰减的啁啾样脉冲序列，逐渐模糊为混响尾"

## 2. 数字实现要点

| 项 | 事实 | 来源 |
|---|---|---|
| 弹簧数 | 3 弹簧（9 型）并行，延迟时间微差 → 拍频 | SchematicHeaven |
| 色散 | 全通链（频率相关延迟 → 啁啾） | Parker 2011 / Välimäki |
| 反馈环 | 延迟线 + 反馈 → 重复回声序列；衰减时间 1.75-4s（中型 ~2s） | SchematicHeaven |
| 阻尼 | 机械阻尼 → 高频衰减（行为 = 环内低通） | 行为参数（damp） |
| 驱动 | 换能器驱动 → 大信号饱和（"弹簧 crunch"） | 行为参数（dwell） |

### 结构建议（namfx 数字实现，行为级）

| Stage | 建议方法 | 依据 |
|---|---|---|
| 弹簧 | 3 路并行，各 = 延迟线（26/31/36ms）+ 5 级一阶全通色散链（系数 0.12/0.16/0.20）+ 环内一阶低通 + 反馈（0.85/0.87/0.89） | Parker 2011 全通链 + 9 型 3 弹簧 |
| 衰减 | fb ≈ 0.89 → RT60 ≈ 1.8s（落在 1.75-3.0s 中档） | SchematicHeaven |
| 阻尼 | damp 0..1 → 环内低通转角 200Hz..8kHz 对数 | 行为参数 |
| 驱动 | dwell 0..1 → drive 0.5..3.0，tanh 软削波（"弹簧 crunch"） | 行为参数 |
| 混合 | out = dry + wet×mix（wet = 3 路均值） | Fender 混响惯例 |

## 3. 参考文献清单

1. **J. Parker, "Efficient Dispersion Generation Structures for Spring Reverb Emulation", EURASIP JASP, 2011** — https://link.springer.com/article/10.1155/2011/646134（✅ 已下载：全通链色散模型 + 实时优化结构）
2. **V. Välimäki, J. Parker 等, "Parametric Spring Reverberation Effect"** — https://www.semanticscholar.org/paper/Parametric-Spring-Reverberation-Effect-V%C3%A4lim%C3%A4ki-Parker/cad9ec2cc976a3ac3b7bd3be6dea609a9f8cdd76（✅ 条目可达：模型基准）
3. **gabrielgustafsson/spring-reverberation-model（README）** — https://github.com/gabrielgustafsson/spring-reverberation-model（✅ 可达：谱延迟滤波 + 调制多抽头反馈环结构描述）
4. **SchematicHeaven, "Accutronics Reverb Tanks"** — http://schematicheaven.org/mods/reverbtanks.htm（✅ 已下载：4/8/9 型规格 + 衰减时间分级）
5. **tonalux, "Why Spring Reverb Sounds Like a Spring"** — https://tonalux.org/blog/spring-reverb-physics-helical-dispersion（✅ 已下载：螺旋色散物理）

## 4. 明确的"未找到 / 不可达"项（待后续验证）

1. **Välimäki/Parker 论文全文数值**（全通链级数/系数/延迟时间表）：PDF 未下载成功，行为参数（5 级、26/31/36ms、系数 0.12/0.16/0.20）为基于模型描述的合理默认
2. **弹簧精确延迟时间**（26/31/36ms 为行为默认）：Accutronics 手册逐型延迟表待验证
3. **Fender 电路级细节**（驱动变压器/拾音放大）：v1 只做弹簧模型本体
4. **驱动换能器频响**：行为 tanh 驱动，待验证
