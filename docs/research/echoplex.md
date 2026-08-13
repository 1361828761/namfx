# 磁带延迟效果器电路事实调研（Maestro Echoplex EP-3 风格）

> 用途：为 namfx 自研"磁带延迟"数字建模（Maestro Echoplex EP-3 风格）提供**有来源的**电路事实。
> 原则：所有数值均来自下方引用来源；凭记忆推测的数值一律不写。来源不可达时明确标注"待验证"。
> 调研日期：2026-08-14。

## 0. 可达性总结

| 来源 | 状态 | 说明 |
|---|---|---|
| **Vintage Technology Archive "Echoplex EP-3 Specs & History"** | ✅ 可达（页面已下载） | **权威来源**：1970 年、固态晶体管电路、控制 = Echo Sustain / Record Level / Echo Volume |
| **vibes.starlite-campbell "The Maestro Echoplex EP-3"** | ✅ 可达（页面已下载） | 磁带延迟原理（带速 + 录放磁头间距决定延迟时间 + 反馈多次回声）+ wow/flutter 来源（磁带过磁头） |
| **EchoFarm 用户手册（Line 6，EP-3 磁带延迟建模）** | ✅ 可达（PDF 已下载并全文抽取） | EP-3 建模视角：Bass/Treble 音调控制、"较不畸变的磁带仿真"（对比 EP-1 的 wow/flutter/失真控制）；Van Halen/Jimmy Page 用户 |
| **obsoletemedia "Echoplex (1961-1991)"** | ✅ 可达（页面已下载） | EP-3 为晶体管版本（EP-1/2 为电子管） |

**关键结论：EP-3 = 固态磁带延迟，控制 = Echo Sustain（反馈）/ Record Level / Echo Volume + Bass/Treble 音调；精确延迟时间范围（60-660ms 为业界通说）未获本会话来源确认 → 行为参数并标记待验证。**

## 1. 电路事实

### 1.1 器件与版本（VTA / obsoletemedia）

| 项 | 值 | 说明 |
|---|---|---|
| 版本 | EP-1/EP-2（电子管，1959-1960s）→ **EP-3（晶体管固态，1970）** | "A transister version, the EP-3"（obsoletemedia） |
| 电路 | 固态晶体管电路 + 磁带循环 | "solid-state transistor-based circuitry and a magnetic tape loop"（VTA） |
| 控制 | **Echo Sustain / Record Level / Echo Volume** | VTA |
| 音调 | Bass / Treble 旋钮 | EchoFarm 手册（EP-3 建模控制 1/2 = Bass/Treble） |
| 知名用户 | Jimmy Page、Eddie Van Halen | EchoFarm 手册 |

### 1.2 磁带延迟原理（starlite-campbell）

```
吉他输入 → 录音磁头（磁带记录）
  → 放音磁头（回放；磁带速度 × 录放磁头间距 = 延迟时间）
  → 反馈（Echo Sustain）：延迟声回注录音输入 → 多次回声
  → 磁带过磁头的速度不稳 = wow/flutter（音高晃动，"与干声混合产生合唱式随机失谐"）
  → 磁带饱和 = 压缩/失真（EP-3 较 EP-1 干净，仍保留磁带特性）
```

## 2. 数字实现要点

| 项 | 事实 | 来源 |
|---|---|---|
| 延迟机制 | 带速 + 磁头间距 → 延迟时间；反馈 → 多次回声 | starlite-campbell |
| 控制 | Echo Sustain（反馈）/ Echo Volume / Record Level；Bass/Treble 音调 | VTA / EchoFarm |
| wow/flutter | 磁带过磁头速度不稳 → 音高晃动（与干声混合产生合唱式失谐） | starlite-campbell |
| 饱和 | 磁带记录饱和 → 压缩/轻度失真（EP-3 比 EP-1 干净） | EchoFarm |
| 延迟范围 | 精确值本会话未获来源；行为默认 60-660ms（业界通说，**待验证**） | 待验证 |

### 结构建议（namfx 数字实现，行为级）

| Stage | 建议方法 | 依据 |
|---|---|---|
| 延迟线 | 复用 `FractionalDelay`（bbd_delay.h） | PLAN §6 行为级 + 既有基建 |
| 延迟时间 | time 0..1 → 60..660ms 线性映射（**范围待验证**） | 待验证 |
| wow/flutter | 双正弦 LFO（~0.7Hz wow ±0.2% + ~6Hz flutter ±0.05%）调制延迟时间（v1 固定量，不暴露参数） | starlite-campbell（行为参数） |
| 饱和 | 湿路径 tanh 软削波（归一化，小信号增益 ≈1，大信号压缩） | EchoFarm "磁带饱和"（行为参数） |
| 湿滤波 | 2 阶 Butterworth 低通，tone 0..1 → fc 3kHz..8kHz 对数扫频（磁带频响 + EP-3 Treble 控制） | EchoFarm Bass/Treble（行为参数） |
| 反馈 | echo 0..1 → 0..0.9，滤波+饱和在环内 → 重复逐次劣化 | VTA Echo Sustain |
| 混合 | out = dry + wet×level（level = Echo Volume） | VTA |

## 3. 参考文献清单

1. **Vintage Technology Archive, "Maestro Echoplex EP-3 — Specs & History"** — https://vintagetechnologyarchive.com/synth/maestro/echoplex-ep-3/（✅ 已下载：1970/晶体管/控制清单）
2. **vibes.starlite-campbell, "The Maestro Echoplex EP-3"** — https://vibes.starlite-campbell.com/p/the-maestro-echoplex-ep-3（✅ 已下载：磁带延迟原理 + wow/flutter 机制）
3. **Line 6, "EchoFarm User Manual"（EP-3 磁带延迟建模）** — https://www.barryrudolph.com/recall/manuals/echofarmuser.pdf（✅ 已下载并全文抽取：Bass/Treble 控制 + EP-1/EP-3 建模差异）
4. **Museum of Obsolete Media, "Echoplex (1961-1991)"** — https://obsoletemedia.org/echoplex/（✅ 已下载：EP-3 晶体管版本确认）

## 4. 明确的"未找到 / 不可达"项（待后续验证）

1. **延迟时间精确范围**（60-660ms 为业界通说，本会话未获一手来源）：行为默认 60-660ms，待 EP-3 手册可达后核对
2. **磁带频响精确曲线**（带速 7.5ips 等）：行为 tone 3kHz..8kHz，待验证
3. **wow/flutter 精确量**：行为固定 wow ±0.2%/0.7Hz + flutter ±0.05%/6Hz，待验证
4. **Record Level / 磁带饱和曲线**：行为 tanh 归一化软削波，待验证
5. **EP-3 前级（"EP-3 preamp" 名声）**：v1 只做延迟路径，前级驱动特性待后续模块
