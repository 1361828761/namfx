# tests/fuzz — 故障注入目标清单（M1+ 填充）

对应 PLAN §12 故障注入项。本目录当前为占位，实现随各模块落地。

| 目标 | 注入方式 | 关联里程碑 |
|---|---|---|
| 半截 .nam | 截断/拼接 .nam 字节流 | M4 |
| 随机 bytes wav | 随机字节冒充 WAV | M3 |
| schema 突变 JSON | 字段乱序/删改/未知字段 | M1 |
| OOM 模拟 | 分配失败注入 | M1+ |
| 采样率切换注入 | prepare 中途切采样率/block size | M1 |
| 宿主任意 block size | 随机 block 长度 | M1 |
| 线程竞态 | 音频线程 vs 加载线程同时改图 | M1 |
| 备份损坏 | 截断临时文件/旋转边界/全损坏恢复 | M1 |
| 迁移链 fuzz | 版本跳档/未知字段/中间态崩溃 | M1 |
| 仲裁交错 | CC+UI+场景并发/hang-time 边界 | M5 |
| 场景 retrigger | 连续快速 recall/淡化中 retrigger/未知参数 ID | M5 |
| 掉电写撕裂模拟 | kill -9/断电点注入 | M1 |
| 14-bit CC MSB/LSB 乱序 | 乱序/缺包 | M5 |
