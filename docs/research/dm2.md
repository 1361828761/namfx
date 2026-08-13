# 模拟延迟效果器电路事实调研（Boss DM-2 风格，MN3005 BBD 延迟线）

> 用途：为 namfx 自研"延迟"数字建模（Boss DM-2 Analog Delay 风格）提供**有来源的**电路事实。
> 原则：所有数值均来自下方引用来源；凭记忆推测的数值一律不写。来源不可达时明确标注"待验证"。
> 调研日期：2026-08-14。

## 0. 可达性总结

| 来源 | 状态 | 说明 |
|---|---|---|
| **AionFX "Amethyst Analog Delay / BOSS DM-2" 项目页** | ✅ 可达（页面已下载） | **权威来源**：1981 年发布、MN3005 BBD **4096 级**、**约 300ms 延迟**、**compander 降噪**、1982 V2 = MN3205/MN3102（音频部分无改动） |
| **ElectroSmash "Boss DM-2 analysis"** | ❌ 不可达 | 本机网络不可达（与既往调研一致）。**待后续验证** |
| **Boss 官方产品页 / hobby-hour 原理图** | ❌ 不可达 | 本机网络不可达。**待后续验证** |

**关键结论：以 AionFX（DM-2 直改复刻项目）为电路事实基准；输出滤波转角由 4096 级 + 300ms 约束推导（见 §2.2）。**

## 1. 电路事实（AionFX，DM-2 V1）

### 1.1 关键器件与拓扑

| 项 | 值 | 说明 |
|---|---|---|
| BBD | **MN3005**（4,096 级） | "bucket-brigade delay chip, 4,096 stages, capable of around 300ms of delay time when properly calibrated" |
| 时钟 | MN3101 类 | BBD 配套时钟；延迟时间旋钮调时钟频率 |
| compander | 有（官方电路） | "uses a compander to significantly improve noise performance for the delay line"——BBD 噪声性能关键 |
| 版本 | 1981 V1（MN3005）/ 1982 V2（MN3205，音频部分无改动） | 建模以 V1 为基准 |
| 控制 | Delay Time / Echo / Intensity 三旋钮 | DM-2 面板（三旋钮） |

### 1.2 信号路径（DM-2 家族标准）

```
吉他输入
  → 输入缓冲/预放大
  → 干声直接输出（dry）
  → 湿路径：输入 + 反馈 → BBD 延迟线（MN3005，时钟调延迟时间）
    → compander（降噪）
    → 输出低通滤波（去时钟噪声）→ 与干声混合输出（Echo 控制湿音量）
  → Intensity（反馈）把滤波后的延迟信号送回延迟输入 → 多次重复
```

## 2. 数字实现要点

| 项 | 事实 | 来源 |
|---|---|---|
| 延迟线 | MN3005 4096 级 BBD，~300ms 最大延迟；延迟时间 = N/(2·fclk) | AionFX |
| 延迟范围 | ~20-300ms（旋钮范围，行为映射） | AionFX（300ms 上限） |
| 反馈 | Intensity 旋钮 → 重复次数，可近自振 | DM-2 面板惯例 |
| 湿路径滤波 | 输出低通去时钟噪声；**转角上限推导**：300ms 时最低时钟 fclk_min = 4096/(2·0.3s) ≈ 6.8kHz → 输出滤波必须 < fclk/2 ≈ **3.4kHz**（"暗回声"的电路根源） | AionFX + BBD 原理推导 |
| 降噪 | compander（行为级 v1 不建模，见 §4.2） | AionFX |

### 结构建议（namfx 数字实现，行为级）

| Stage | 建议方法 | 依据 |
|---|---|---|
| 延迟线 | 复用 `FractionalDelay`（bbd_delay.h，线性插值分数延迟） | PLAN §6 行为级 + 既有基建 |
| 延迟时间 | time 0..1 → 20..300ms 线性映射 | AionFX 300ms 上限 |
| 反馈 | feedback 0..1 → 0..0.9（低于自振 1.0），滤波后信号回注延迟输入 | DM-2 Intensity 惯例 |
| 湿滤波 | 2 阶 Butterworth 低通，fc ≈ 3.5kHz（由 §2 推导上界定；精确转角**待验证**）——在反馈环内 → 重复逐次变暗 | 推导 + "暗回声"共识 |
| 混合 | out = dry + wet×level（level = Echo 湿音量） | DM-2 Echo 旋钮 |
| 降噪 | compander v1 不建模（无 BBD 电荷转移噪声需压制） | — |

## 3. 参考文献清单

1. **AionFX, "Amethyst Analog Delay / BOSS DM-2"（项目页）** — https://aionfx.com/project/amethyst-analog-delay/（✅ 已下载：MN3005 4096 级 + 300ms + compander + V1/V2 说明）
2. **ElectroSmash, "Boss DM-2 analysis"** — https://www.electrosmash.com/boss-dm-2-analysis（❌ 不可达，**待后续验证**：输出滤波精确转角/元件值）
3. **hobby-hour, "Boss DM-2 Delay schematic"** — https://www.hobby-hour.com/electronics/s/dm2-delay.php（❌ 不可达，**待后续验证**：原理图）

## 4. 明确的"未找到 / 不可达"项（待后续验证）

1. **输出滤波精确转角**：由 4096 级 + 300ms 推导上界 ≈3.4kHz，行为默认 3.5kHz 二阶；待 electrosmash/hobby-hour 可达后核对
2. **compander 特性**（噪声门限/扩展曲线）：官方提及但无参数；v1 不建模
3. **BBD 电荷转移噪声/颗粒感**（chowdsp BBDDelay 风格）：v1 行为级插值延迟线替代（与 flanger.md §5.3 一致）
4. **延迟时间旋钮锥度**（行为线性映射；原踏板锥度待验证）
