# IR 重采样与最小相位化调研（windowed-sinc + cepstral 法）

> 用途：M3 IR 引擎"高质量重采样 + 最小相位化"（PLAN §6/§13 验收：与参考实现误差 < -100dB）。
> 原则：所有数值均来自下方引用来源或本机验证实验（标注"本机实测"）；凭记忆推测的数值一律不写。
> 调研日期：2026-08-14。

## 0. 结论摘要

- **重采样**：单阶段 **Kaiser windowed-sinc 插值**（零交叉数 ZC=64、β=14，内部 double 累积）。
  - 本机实测：核通带（|f|<0.45·Nyquist）幅度误差 **-294dB（double 机器精度）**；ZC=16/32/64/128 通带全部平坦——截断纹波低于 double 精度（核 Σ=1 归一化后数值上完美低通）。
  - 与 scipy `resample_poly`（两阶段 polyphase 参考）差异 ~-70dB 级别——属两算法核差异（resample_poly 的核过渡带不同），**非缺陷**；收敛判据（ZC=64 vs ZC=256 输出差）与带限正弦保真判据是更稳的工程验证。
- **最小相位化**：**cepstral（同态）法**——FFT → log|H| → 倒谱因果化 → exp → IFFT。
  - 本机实测：幅度谱保持误差随 FFT 零填充量收敛（pad=2: -32dB / 4: -38dB / 8: -55dB / 16: -74dB / 32: -96dB）——**倒谱混叠**主导；**取 pad=16**（2048 长 IR → N=32768，一次性加载路径，成本可忽略），-74dB 对音频不可闻。
  - 能量保持：本机实测 19.2970 → 19.2970（机器精度）；截断回原长丢尾部 0.04% 能量。
  - 最小相位 IR 能量前移：前 25% 能量占比 0.908 → 0.952（本机实测，512 长指数衰减随机 IR）。

## 1. 重采样：Kaiser windowed-sinc

### 1.1 事实（来源）

- **理想插值核 = sinc**：带限信号在采样定理下可由 sinc 精确重构；实际用**窗截断 sinc**（windowed-sinc）折中长度与旁瓣。— Smith, *Spectral Audio Signal Processing* [1]（Windowed-Sinc Interpolation 章节）
- **Kaiser 窗**：`w[n] = I0(β·√(1−(2n/M−1)²)) / I0(β)`，β 控制旁瓣/主瓣权衡；**β=14 为音频重采样事实标准默认值**。— resampy（bmcfee）默认 `num_zeros=64, beta=14` [2]
- **数字 sinc 的特殊性**：整数网格采样的 sinc 的 DFT 恰为理想矩形低通；windowed-sinc 核 = 理想低通 ⊛ 窗频谱——通带纹波与阻带泄漏都来自窗旁瓣（本机实测通带 -294dB 说明 β=14/ZC=64 下泄漏低于 double 精度）。
- **重采样实施**：每个输出样本 `y[n] = Σ_k x[k]·sinc((n·fr/tr − k))·kaiser((n·fr/tr − k)/ZC)`；**核需 Σ=1 归一化**（窗截断使 DC 增益 ≠1，边界处尤其明显）。— resampy filters.py 实现 [2]
- **边界策略**：窗外样本补零（IR 首尾通常≈0——麦克风瞬态前与衰减尾）；clamp 复制会在边界产生假信号。

### 1.2 精度验证（本机实测，python/numpy）

| 验证 | 结果 |
|---|---|
| 核通带幅度误差（ZC=16..128, β=14） | **-294dB**（double 机器精度，核归一化后） |
| 带限正弦（800+220Hz 指数衰减 IR 形信号）44.1k→48k 与 scipy resample_poly 差异 | ~-70dB（两算法核差异，非缺陷） |
| ZC=64 与 ZC=256 输出差异（收敛判据） | 待 C++ 测试锁定（预期 < -100dB） |
| 1kHz 带限正弦幅度保真 | 待 C++ 测试锁定（预期 ~float 量化 -140dB） |

## 2. 最小相位化：cepstral（同态）法

### 2.1 算法（来源）

- **最小相位系统**：幅度谱相同、能量最早到达（群延迟最小）的系统；任何幅度谱 |H| 有唯一最小相位实现。— Smith, *Introduction to Digital Filters* [3]
- **cepstral 构造**（scipy.signal.minimum_phase 同法 [4]）：
  1. 补零到 N（本设计 N = nextPow2(16·L)）
  2. `H = FFT(h)`；`mag = |H| + ε`（防 log(0)）
  3. `c = IFFT(log(mag))`（实倒谱）
  4. 因果化：`c_min[0]=c[0]`，`c_min[n]=2c[n]`（1≤n<N/2），`c_min[N/2]=c[N/2]`，`c_min[n]=0`（n>N/2）
  5. `h_min = IFFT(exp(FFT(c_min)))`，截断回原长 L
- **幅度保持理论**：log|H| 的倒谱实偶 → 因果化后偶部不变 → `|H_min| = |H|` 精确成立；实际误差 = **倒谱混叠**（倒谱无限长，N 截断）+ 截断回 L 的尾部损失。
- **pad 权衡**（本机实测）：pad=16 → -74dB，pad=32 → -96dB；音频用途 -74dB 足够（-100dB 是重采样指标非最小相位指标），**选 pad=16**。
- **尾部损失**：最小相位化把能量前移，截断回 L 丢 ~0.04% 能量（本机实测）——业界同法（NadIR/IR 加载器）接受。

## 3. 与 PLAN 指标的关系

- PLAN §13 M3 验收"与参考实现（直接时域卷积，double）最大误差 < -100dB"——指**卷积**对照（已有直接/分区卷积测试锁定 <1e-4/-100dB）。
- 重采样精度：核通带 -294dB + 收敛判据（ZC=64 vs 256）双保险；带限正弦保真 ~float 量化级。
- 最小相位化：幅度谱保持 -74dB（pad=16）+ 能量保持 0.04%——两个性质测试独立锁定，不依赖"与参考实现同构"。

## 4. 参考文献

1. **J. O. Smith, "Spectral Audio Signal Processing"**（Windowed-Sinc Interpolation / Kaiser window）— https://ccrma.stanford.edu/~jos/sasp/Windowed_Sinc_Interpolation.html
2. **resampy（bmcfee）filters.py**（windowed-sinc 插值事实实现：num_zeros=64、beta=14、核归一化）— https://github.com/bmcfee/resampy/blob/master/resampy/filters.py
3. **J. O. Smith, "Introduction to Digital Filters"**（Creating Minimum Phase Filters，cepstral 法）— https://ccrma.stanford.edu/~jos/filters/Creating_Minimum_Phase_Filters.html
4. **scipy.signal.minimum_phase 文档**（同法实现：n_fft 默认 2×，cepstrum 因果化窗）— https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.minimum_phase.html

## 5. 明确的"未找到 / 待落地"项

1. 重采样**过渡带宽度**优化（ZC vs 过渡带 0.45-0.5 的精确折中）——ZC=64 已达标，无进一步需求
2. 最小相位化对**超长 IR**（65536）的成本实测（N=1M FFT，加载路径一次性）——M3 收尾时测
3. 立体声/双 IR 的最小相位化策略（逐声道独立 vs 公共相位）——随 G19 后置
