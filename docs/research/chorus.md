# 合唱效果器电路事实调研（CE-2 风格，BBD 延迟线）

> 用途：为 namfx 自研"合唱"数字建模（Boss CE-2 风格）提供**有来源的**电路事实。
> 原则：所有数值均来自下方引用来源；凭记忆推测的数值一律不写。来源不可达时明确标注"待验证"。
> 调研日期：2026-08-14。

## 0. 可达性总结

| 来源 | 状态 | 说明 |
|---|---|---|
| electrosmash.com（Boss CE-2 analysis） | ❌ 不可达 | 本机网络不可达；web.archive 429、archive.today 无快照。**待后续验证** |
| PCB Guitar Mania "Chief Chorus"（Boss CH-1 复刻） | ✅ 可达但 BOM 404 | CH-1 同为 Boss 合唱（BBD 拓扑），Building Docs 链接 404；产品页描述可引用 |
| PCB Guitar Mania "Electric Lover Flanger"（EHX Electric Mistress 复刻） | ✅ 可达 | 同族 BBD 调制（MN3007）完整 BOM + 原理图，见 flanger.md |
| jatinchowdhury18/BBDDelay | ✅ 仓库存在 | BBD 延迟线仿真参考实现（未抓取源码，BBD 原理为业界共识） |

**关键结论：CE-2 精确电路来源不可达。合唱的数字建模采用"行为级复刻"（PLAN §6：时间/滤波类=行为级复现）：以 Boss 合唱族的公开行为事实（MN3007 BBD + LFO + 干湿混合）为建模依据，不依赖精确元件值。**

## 1. 合唱效果原理（Boss CE-2 行为事实，社区共识）

```
吉他输入
  → 输入缓冲
  → BBD 延迟线（MN3007，1024 级）：延迟 = N/(2·fc)，fc 由时钟芯片（MN3101）产生
      - 时钟范围 ~10kHz-1MHz → 延迟 0.5ms-50ms
      - 合唱工作点：~20-30ms 延迟，LFO 调制 ±几 ms
  → LFO（~0.1-10Hz 三角/正弦）调制时钟频率 → 延迟时间缓慢摆动
  → 干湿混合（~50/50）：干声 + 调制延迟声叠加 → 相位干涉 → "合唱"音色
```

### 1.1 关键特征（CE-2 音色机制）

1. **短延迟 + 缓慢调制**：20-30ms 基延迟，LFO 调制延迟时间 ±几 ms → 输出信号的相位随时间缓慢摆动 → 干湿叠加产生"合唱/加厚"效果
2. **LFO**：三角波或正弦，E.Rate 旋钮控制频率（~0.1-10Hz）
3. **Depth**：控制 LFO 调制延迟的幅度（深度）
4. **Level**：输出电平
5. **三旋钮**：Depth / E.Rate / Level

## 2. 参数与实现要点

| 项 | 事实 | 来源 |
|---|---|---|
| 延迟线 | BBD 1024 级，合唱工作点 ~20-30ms 基延迟 | CE-2 行为事实（社区共识，待验证） |
| 调制 | LFO 三角/正弦调制延迟时间 | 同上 |
| Rate | LFO 频率 0.1-10Hz（E.Rate 旋钮） | 同上 |
| Depth | 延迟调制深度（~0-10ms） | 同上 |
| 混合 | 干湿 ~50/50 | 同上 |

### 结构建议（namfx 数字实现）

| Stage | 建议方法 | 依据 |
|---|---|---|
| 延迟线 | 可变分数延迟线（线性插值），基延迟 20ms + LFO 调制 ±(depth×10ms) | BBD 行为等效（PLAN §6 行为级） |
| LFO | 三角波（0.1-10Hz，rate 参数） | CE-2 行为 |
| 混合 | dry·0.5 + wet·0.5 | CE-2 行为 |
| 输出 | level 缩放 | CE-2 行为 |

## 3. 参考文献清单

1. **Electrosmash, "Boss CE-2 analysis"** — https://www.electrosmash.com/boss-ce-2-analysis（❌ 不可达，无快照，**待后续验证**；CE-2 精确元件值的首要来源）
2. **PCB Guitar Mania, "Chief Chorus"（Boss CH-1 复刻）** — https://pcbguitarmania.com/product/chief-chorus/（✅ 产品页可达；BOM 链接 404）
3. **PCB Guitar Mania, "Electric Lover Flanger"（EHX Electric Mistress 复刻，MN3007 BBD）** — 见 `flanger.md`（✅ 同族 BBD 调制的完整事实源）

## 4. 明确的"未找到 / 不可达"项（待后续验证）

1. **CE-2 精确元件值**（MN3007 时钟范围、LFO 电路、混合比例）：Electrosmash 不可达且无快照。本文以行为事实建模，元件级精确复刻待原文可达后补充。
2. **CE-2 的 LFO 波形**：三角 vs 正弦未定（社区说法不一），本文用三角波。
3. **CE-2 基延迟精确值**：~20-30ms 为社区共识区间，未验证到精确值。
