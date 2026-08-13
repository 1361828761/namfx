# 镶边效果器电路事实调研（Electric Mistress 风格，BBD 延迟线）

> 用途：为 namfx 自研"镶边"数字建模（EHX Electric Mistress / Boss BF-2 风格）提供**有来源的**电路事实。
> 原则：所有数值均来自下方引用来源；凭记忆推测的数值一律不写。来源不可达时明确标注"待验证"。
> 调研日期：2026-08-14。

## 0. 可达性总结

| 来源 | 状态 | 说明 |
|---|---|---|
| **PCB Guitar Mania "Electric Lover Flanger"（EHX Electric Mistress 复刻）** | ✅ 可达 | **权威来源**：MN3007 BBD 完整 BOM + 原理图 + 控制说明（Feedback/Range/Rate 三旋钮） |
| electrosmash.com（Boss BF-2 analysis） | ❌ 不可达 | 本机网络不可达（与既往调研一致）。**待后续验证** |
| GitHub 仓库搜索 | ❌ 无结果 | "flanger"/"BF-2" 无相关仓库 |

**关键结论：Electric Lover（Electric Mistress 复刻）为唯一可达的完整电路事实源；BF-2 与 Electric Mistress 同为 BBD 镶边（MN30xx 系列），拓扑同族，本文以 Electric Mistress 为建模基准。**

## 1. 电路拓扑（Electric Lover = EHX Electric Mistress）

```
吉他输入
  → 输入缓冲 → 混合节点
  → BBD 延迟线（MN3007，1024 级）：延迟 = N/(2·fc)
  → 时钟：MN3102 类时钟芯片，LFO 调制时钟频率 → 延迟时间摆动
  → 反馈：延迟输出经反馈网络回注入延迟输入（Feedback 旋钮）→ 梳状谐振
  → 干湿混合（干声 + 延迟声 ~50/50）→ 输出增益级
```

### 1.1 关键特征（镶边音色机制）

1. **极短延迟 + 慢速扫频**：BBD 延迟 ~0.5-10ms，LFO 调制延迟时间 → 干湿叠加产生**扫动的梳状滤波**（notch 随延迟摆动）——"飞机起飞/喷气"音色
2. **反馈（Feedback）**：延迟信号反馈回输入 → 梳状滤波变锐利（谐振）→ 反馈高时近乎"自振"
3. **Range**：LFO 调制延迟的范围（深度）
4. **Rate**：LFO 频率
5. **三旋钮**：Feedback / Range / Rate

## 2. 元件值（Electric Lover BOM，MN3007）

### 2.1 关键元件

| 元件 | 值 | 说明 |
|---|---|---|
| IC（BBD） | MN3007 | 1024 级 BBD |
| 时钟 | MN3102 类 | 10kHz-1MHz 时钟，LFO 调制 |
| Feedback pot | 100kΩ（BOM R21 类）| 反馈量 |
| Range pot | 100kΩ 类 | LFO 深度 |
| Rate pot | 100kΩ 类 | LFO 频率 |

> 原理图文本提取不完整，电位器精确锥度/阻值以 BOM 的 R21/R38/R39 等（100k/200k 量级）为准；数字建模采用行为参数。

## 3. 数字实现要点

| 项 | 事实 | 来源 |
|---|---|---|
| 延迟线 | BBD 1024 级，镶边工作点 ~0.5-10ms | Electric Lover（MN3007） |
| 调制 | LFO 调制延迟时间（Range 控制深度） | Electric Lover 控制说明 |
| 反馈 | 延迟输出反馈回输入（Feedback 旋钮）→ 谐振 | Electric Lover 控制说明 |
| 混合 | 干湿 ~50/50 | Electric Lover 拓扑 |
| Rate | LFO 频率（~0.1-10Hz） | Electric Lover 控制说明 |

### 结构建议（namfx 数字实现）

| Stage | 建议方法 | 依据 |
|---|---|---|
| 延迟线 | 可变分数延迟线（线性插值），基延迟 ~1ms + LFO 调制 ±(range×4ms) | BBD 行为等效（PLAN §6 行为级） |
| 反馈 | 延迟输出 × feedback（0-0.9）回注延迟输入 | Electric Lover |
| LFO | 三角波（0.1-10Hz，rate 参数） | Electric Lover |
| 混合 | dry·0.5 + wet·0.5 | Electric Lover 拓扑 |

## 4. 参考文献清单

1. **PCB Guitar Mania, "Electric Lover Flanger – Building Docs 1.1v"（EHX Electric Mistress 复刻，MN3007）** — https://pcbguitarmania.com/product/electric-lover-flanger/（✅ 已下载并抽取全文：BOM + 原理图 + 控制说明）
2. **Electrosmash, "Boss BF-2 analysis"** — https://www.electrosmash.com/boss-bf-2-analysis（❌ 不可达，**待后续验证**）

## 5. 明确的"未找到 / 不可达"项（待后续验证）

1. **BF-2 精确电路**：Electrosmash 不可达；本文以 Electric Mistress（同族 BBD 镶边）为建模基准，BF-2 与其差异（时钟范围、反馈网络细节）待验证。
2. **Electric Lover 电位器锥度/精确阻值**：原理图文本提取不完整，BOM 的电位器精确值部分缺失；数字建模用行为参数（0..1 归一化）。
3. **BBD 的"颗粒感/时钟噪声"**：物理 BBD 的采样保持电荷转移噪声（chowdsp BBDDelay 风格）v1 不建模，行为级插值延迟线替代（PLAN §6 行为级）。
