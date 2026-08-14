# IR 引擎调研（箱体/空间卷积：WAV 加载 + 重采样 + 分区卷积）

> 用途：为 namfx 自研"IR 卷积"（箱体/空间模拟，PLAN §6）提供**有来源的**事实。
> 原则：所有数值均来自下方引用来源；凭记忆推测的数值一律不写。
> 调研日期：2026-08-14。

## 0. 可达性总结

| 来源 | 状态 | 说明 |
|---|---|---|
| **W. Gardner, "Efficient Convolution without Input/Output Delay", JAES 43(3), 1995** | ✅ 可达（论文条目 + 综述引用） | **分区卷积经典**：均匀分区重叠保留（UPOLS）——把长 IR 分成均匀块，每块 FFT 频域相乘 + 重叠保留拼接，低延迟实时卷积 |
| **tonalux, "Partitioned Convolution: The Algorithm That Makes Real-Time Reverb Possible"** | ✅ 可达（条目） | 分区卷积综述：长 IR 实时卷积只能靠分区 FFT；非均匀分区（短块在前降延迟）改进 |
| **cabIR.eu, "IR formats READ-ME"** | ✅ 可达（条目） | 吉他箱体 IR 常见格式说明（WAV：16/24-bit PCM、float；44.1/48k 为主） |

**关键结论：M3 v1 = WAV 加载 + 重采样 + 直接时域卷积（确定性、可对照 double 参考 < -100dB）；UPOLS 分区卷积（FFT）为性能路径 v2（IR 长度上限先收紧，直接卷积可实时）。**

## 1. 算法事实

### 1.1 分区卷积（Gardner 1995 / tonalux）

- **直接时域卷积**：y[n] = Σ_k h[k]·x[n−k]，每样本 O(L) 乘加（L = IR 长度）——短 IR 可行，长 IR 不可实时
- **均匀分区重叠保留（UPOLS）**：IR 分成 P 块（每块 M 样本）→ 每块与输入块做 FFT 卷积 → 各块输出按输入块对齐累加；延迟 = 一个块长 M
- **非均匀分区**：前几块短（低延迟）、后面长（高效率）——tonalux 指出"短块在前"是低延迟关键
- 参考精度契约（PLAN §13 M3 验收）：**与参考实现（直接时域卷积，double 精度）最大误差 < -100dB**

### 1.2 IR 资产（cabIR.eu）

- 吉他箱体 IR = WAV 文件（常见 16/24-bit PCM 与 float 格式；44.1k/48k 采样率为主）
- 加载路径要求：PCM 16/24/32 + IEEE float32/64；立体声 IR 取单声道（双 IR/立体声 IR 后置，保持单声道决策）
- 采样率 ≠ 引擎率时需重采样（PLAN：一次性高质量重采样并缓存；最小相位化后置）

### 1.3 超长 IR 策略（PLAN §6 质量约束）

- 超长 IR 按平台档位 `max_ir_seconds` **上限拒绝（不静默截断——截断会改变音色）**

## 2. 数字实现要点（namfx IR 引擎 v1）

| 项 | 事实/建议 | 来源 |
|---|---|---|
| WAV 解析 | 手写 RIFF 解析（PCM 16/24/32 + float32/64，mono/stereo 取 L）——core 零依赖红线（解析在加载路径，非回调内） | cabIR.eu 格式 + 红线 2 |
| 重采样 | 线性插值重采样（v1；窗函数 sinc 高质量 + 最小相位化后置） | PLAN §6 |
| 卷积 | v1 直接时域卷积（double 参考对照 < -100dB）；**UPOLS FFT 分区卷积 v2**（自写 radix-2 FFT，非均匀分区低延迟） | Gardner 1995 |
| 上限 | IR ≤ 65536 样本（~1.37s@48k），超长拒绝报错（不静默截断）；直接卷积实时性由上限保证 | PLAN §6 |
| 参数 | gain（0..1 → −12..+6dB）；低切/高切/位置参数后置 | PLAN §6 |
| 加载路径 | 预设 slot 增加 file 字段（PLAN §7 预设格式 v1 计划内字段，补实现）；baseDir 解析相对路径 | PLAN §7 |
| 实时安全 | 卷积环形缓冲 + IR 副本 prepare 预分配，回调内零分配 | 红线 1 |

## 3. 参考文献清单

1. **W. Gardner, "Efficient Convolution without Input/Output Delay", JAES 43(3), 1995** — https://www.semanticscholar.org/paper/Efficient-Convolution-without-Input-Output-Delay-Gardner/b957c1c8ea331a6138b907adfcc9ad6a10c14d1e（分区卷积经典，UPOLS）
2. **tonalux, "Partitioned Convolution: The Algorithm That Makes Real-Time Reverb Possible"** — https://tonalux.org/blog/partitioned-convolution-realtime-reverb（分区卷积综述 + 非均匀分区）
3. **cabIR.eu, "IR formats READ-ME-FIRST"** — https://www.cabir.eu/img/cms/manuals/cabIR.eu_pro-ir-series_IR-formats_READ-ME-FIRST-BEFORE-DOWNLOAD_en.pdf（吉他箱体 IR 格式说明）

## 4. 明确的"未找到 / 待落地"项

1. **UPOLS 块长/分区数优化**（延迟 vs 效率权衡）：v2 随 FFT 落地时实测
2. **高质量重采样（窗函数 sinc）**：v1 线性插值；精度要求（重采样误差 < -100dB 参考）随 v2 升级
3. **最小相位化**（IR 相位整理）：同态滤波/倒谱，后置
4. **立体声/双 IR**：保持单声道决策，后置（G19 立体声扩展时一并评估）
