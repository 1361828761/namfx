# 音高家族调研（移调核心：YIN 检测 + WSOLA/延迟线移调）

> 用途：为 namfx 自研"音高家族"（移调/八度/whammy/和声器，单音假设）提供**有来源的**算法事实。
> 原则：所有数值均来自下方引用来源；凭记忆推测的数值一律不写。
> 调研日期：2026-08-14。

## 0. 可达性总结

| 来源 | 状态 | 说明 |
|---|---|---|
| **YIN 算法（de Cheveigné & Kawahara, JASA 2002）** | ✅ 可达（论文 + 开源实现） | 基频检测基准：差分函数 + 累积均值归一化（CMND）+ 阈值 + 抛物线插值 |
| **winkt0/yin_rs（Rust YIN 实现）** | ✅ 可达 | YIN 参考实现（含抛物线插值细化），开源 |
| **WSOLA（Verhelst & Roelands, ICASSP 1993）** | ✅ 可达（综述/实现） | 波形相似重叠相加：时间缩放标准算法 |
| **resemble-ai/PyTSMod（开源 Python TSM 库）** | ✅ 可达 | WSOLA/相位声码器参考实现 |
| **J. Bonada 博士论文（UPF 2002）** | ✅ 可达（PDF 链接） | 音乐信号时间缩放综述基准 |

**关键结论：移调核心 v1 = 固定比率单音移调（延迟线 + 周期交叉淡化，DAFX 教材路线，实时安全、确定性）；YIN 检测器与和声器/八度模块（需要检测）随后续模块落地。PLAN §6：音高家族单音假设 + 移调核心优先 + 算法延迟 10-30ms 豁免 5ms 断言。**

## 1. 算法事实

### 1.1 音高检测：YIN（de Cheveigné & Kawahara, 2002）

1. **差分函数**：d(τ) = Σ_{j=1}^{W} (x[j] − x[j+τ])²（W 为分析窗）
2. **累积均值归一化差分函数（CMND）**：d'(τ) = d(τ) / [(1/τ)·Σ_{j=1}^{τ} d(j)] ——消除随 τ 增长的偏差
3. **阈值**：取第一个低于阈值（通常 0.1-0.15）的 CMND 谷值 → 候选周期 τ₀
4. **抛物线插值**：在 τ₀ 邻域拟合抛物线细化周期 → f0 = fs/τ
5. 无低于阈值的谷值 = 非周期段（无音高）→ 不适用移调
6. 分析窗/跳帧（hop）决定延迟与刷新率：窗 20-50ms 量级

### 1.2 时间缩放/移调：WSOLA（Verhelst & Roelands, 1993）

- **波形相似性重叠相加**：分析段在输入中按跳帧移动；合成段在输出中重叠相加，段间按**波形相似度对齐**再混合（vs 纯 OLA 的对齐无关）
- 移调 = 时间缩放 + 重采样：时间缩放 α 后按 1/α 重采样 → 音高变 α、时长不变
- 质量关键：段长（20-40ms 量级）+ 对齐 + 重叠窗（Hann 类）

### 1.3 实时移调：延迟线 + 周期交叉淡化（DAFX 教材路线）

- 固定比率 r 的实时实现：延迟线读指针按 r 前进（重采样）；读指针追上写指针（r>1）或落后超限（r<1）时**跳回一个交叉淡化窗长度 C**，跳变期间两条读路径（相距 C）线性交叉淡化 → 隐藏相位不连续
- 交叉淡化窗 C ≈ 10-30ms；最大延迟 D 决定最低移调（r<1 时延迟 ≈ D）
- 固有算法延迟 ≈ C..D（音高模块豁免 5ms 断言，PLAN §6）

## 2. 数字实现要点（namfx 移调核心 v1）

| 项 | 事实/建议 | 来源 |
|---|---|---|
| 移调 | 固定比率 r = 2^(ST/12)，ST = −12..+12 半音（shift 参数 0..1） | 行为参数 |
| 引擎 | 延迟线（D=100ms 预分配）+ 读指针按 r 重采样（线性插值）+ 周期交叉淡化（C=20ms） | DAFX 教材路线（§1.3） |
| 混合 | out = dry·(1−mix) + shifted·mix（八度/混音惯例） | 行为参数 |
| 检测 | v1 不做基频检测（固定比率即可用）；YIN 检测器随八度/和声器模块落地 | §1.1（后续模块） |
| 延迟 | 固有算法延迟 C..D（20-100ms 量级），豁免 5ms 断言 | PLAN §6 |

## 3. 参考文献清单

1. **A. de Cheveigné, H. Kawahara, "YIN, a fundamental frequency estimator for speech and music", JASA 111(4), 2002** — 差分函数/CMND/阈值/抛物线插值（经开源实现核实：https://github.com/winkt0/yin_rs）
2. **W. Verhelst, M. Roelands, "An overlap-add technique based on waveform similarity (WSOLA) for high quality time-scale modification of speech", ICASSP 1993** — WSOLA（经开源实现核实：https://github.com/resemble-ai/PyTSMod）
3. **J. Bonada, "Audio Time-Scale Modification in the Context of Professional Audio Post-production", 博士论文, UPF 2002** — https://mtg.upf.edu/files/publications/Phd-2002-Jordi-Bonada.pdf（TSM 综述基准）
4. **U. Zölzer (ed.), "DAFX — Digital Audio Effects"（移调章节：延迟线 + 交叉淡化实时移调）** — 教材路线（v1 引擎结构依据）

## 4. 明确的"未找到 / 待落地"项

1. **YIN 工程参数**（窗长/跳帧/阈值精确值）：随八度/和声器模块做听感调优
2. **McLeod 检测器**（PLAN 备选）：与 YIN 对比测试后再定
3. **相位声码器路线**（PLAN 备选）：v2 候选（和声器多声部时评估）
4. **复音移调**：数月级，明确不做（PLAN §6）
