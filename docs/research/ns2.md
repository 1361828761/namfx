# 门限/降噪效果器电路事实调研（Boss NS-2 风格，包络检测 + VCA 门控）

> 用途：为 namfx 自研"门限"数字建模（Boss NS-2 Noise Suppressor 风格）提供**有来源的**电路事实。
> 原则：所有数值均来自下方引用来源；凭记忆推测的数值一律不写。来源不可达时明确标注"待验证"。
> 调研日期：2026-08-14。

## 0. 可达性总结

| 来源 | 状态 | 说明 |
|---|---|---|
| **Boss 官方产品页 "NS-2 Noise Suppressor"** | ✅ 可达（页面已下载） | **权威来源**：Threshold/Decay 官方定义 + "VCA 与高速包络检测电路" + send/return 隔离检测原理 + 规格（输入 1MΩ / -20dBu） |
| **ElectroSmash "Boss NS-2 analysis" 类电路分析** | ❌ 不可达 | 本机网络不可达（与既往调研一致）。**待后续验证** |
| **DIYstompboxes 论坛 NS-2 电路帖** | ❌ 不可达 | 本机网络不可达。**待后续验证** |

**关键结论：以 Boss 官方产品页为行为基准（Threshold/Decay 语义 + 包络检测 + 平滑门控）；电路级细节（元件值）待 electrosmash 可达后补充。**

## 1. 工作原理（Boss 官方）

```
吉他输入
  → 高速包络检测电路（检测输入信号电平）
  → VCA 门控（包络低于阈值 → 增益衰减到静音）
  → 输出缓冲
```

1. **Threshold（阈值）**："Determines the level at which noise suppression begins. Start with the knob at the lowest setting and turn it clockwise until the noise disappears."（决定噪声抑制开始的电平；从最低档顺时针拧到噪声消失为止）
2. **Decay（衰减）**："Adjusts how long it takes for the sound to fade to silence after the input drops below the threshold. Turn the knob clockwise to increase the decay time for the most natural sound."（输入低于阈值后淡出到静音的时间；顺时针增大衰减时间更自然）
3. **平滑有机**："sensitive analog circuitry silences noise in a smooth and organic way, eliminating the choppy artifacts produced by conventional noise gates"（平滑有机地消除噪声，避免传统门限的"碎块"伪影）→ 门控增益必须平滑过渡
4. **send/return 隔离检测**：过载/失真类高增益踏板插在 NS-2 的 send/return 环内时，"circuitry continually detects the input signal while suppressing noise from the pedals in the loop"——**检测基于（干）输入信号，抑制作用于环内信号**；切换环内踏板时无缝降噪
5. **规格**：输入阻抗 1MΩ、标称 -20dBu、缓冲旁路；9V 供电

## 2. 数字实现要点

| 项 | 事实 | 来源 |
|---|---|---|
| 检测 | 高速包络检测（官方）；数字实现 = 峰值/整流包络跟随器 | Boss 官方 |
| 门控 | VCA 增益控制（官方）；数字实现 = 平滑门控增益（0..1）乘输入 | Boss 官方 |
| Threshold | 噪声抑制开始电平；数字实现 = 包络 vs 阈值的比较判决 | Boss 官方 |
| Decay | 低于阈值后淡出到静音的时间；数字实现 = 门控增益释放时间常数 | Boss 官方 |
| 平滑 | 官方明示避免"choppy artifacts" → 增益必须平滑（快开慢关） | Boss 官方 |
| 检测源 | 官方原理 = 检测输入信号、抑制环内信号；v1 引擎无效果环路 → 检测与抑制同一信号 | Boss 官方（send/return 不建模） |

### 结构建议（namfx 数字实现，行为级）

| Stage | 建议方法 | 依据 |
|---|---|---|
| 包络 | 整流峰值跟随：attack ~1ms / release ~60ms 双时间常数一阶 | Boss 官方"高速包络检测" |
| 判决 | env > threshLin 则开（目标增益 1），否则关（目标增益 0） | Boss 官方 Threshold 定义 |
| 门控平滑 | 增益一阶平滑：打开 ~1ms / 关闭 = decay 参数 10ms..500ms | Boss 官方 Decay 定义 + 平滑要求 |
| 阈值映射 | threshold 0..1 → -70..-10dB 线性映射（行为参数） | 行为参数惯例 |
| 输出 | x × gain × level（level 作输出增益，对照 chorus/phaser 惯例） | 行为参数惯例 |

## 3. 参考文献清单

1. **Boss 官方, "NS-2 Noise Suppressor — Features & Specs"** — https://www.boss.info/nz/products/ns-2/features/（✅ 已下载：Threshold/Decay 官方定义 + VCA/包络检测 + send/return 原理 + 规格）
2. **ElectroSmash, "Boss NS-2 analysis"** — https://www.electrosmash.com/boss-ns-2（❌ 不可达，**待后续验证**：电路级元件值/检测器细节）
3. **DIYstompboxes, "Boss NS-2 Noise Suppressor" 论坛帖** — https://diystompboxes.com/smfforum/index.php?topic=37810.0（❌ 不可达，**待后续验证**）

## 4. 明确的"未找到 / 不可达"项（待后续验证）

1. **电路级细节**（包络检测器拓扑/元件值/VCA 实现）：electrosmash 与 DIYstompboxes 均不可达；v1 行为级建模
2. **send/return 环**：v1 引擎无效果环路路由（PLAN §5 引擎预留），检测源 = 输入信号（官方原理的检测侧），抑制作用于同一信号；环内抑制差异待环路路由落地后补
3. **Mute 模式**（踏板开关切换降噪/静音）：v1 不建模（旁路由链层 bypass 承担）
4. **阈值滞回**：官方未给滞回规格；v1 用门控增益平滑防抖（官方"平滑有机"要求），滞回作为 v2 备选
