# tests/fuzz — 故障注入目标清单（M1 范畴已实现，M3/M5 随模块落地）

对应 PLAN §12 故障注入项。实现载体：`namfx_fuzz`（确定性种子，`--iters N --seed S`；ctest 自动跑 300 轮）。发现 = 崩溃（进程退出）/ 逃逸异常（非 bad_alloc）/ 非有限输出（F3）。

| 目标 | 注入方式 | 状态 | 载体 |
|---|---|---|---|
| schema 突变 JSON | 字段乱序/删改/未知字段/随机字节/截断 | ✅ 已实现 | F1（fuzz_main.cpp） |
| 迁移链 fuzz | 版本跳档/未知字段/类型腐蚀/深层嵌套 | ✅ 已实现 | F2（fuzz_main.cpp） |
| 采样率切换注入 | prepare 中途切采样率 + 随机 block size | ✅ 已实现 | F3（fuzz_main.cpp） |
| 宿主任意 block size | 随机 block 长度 1-512 | ✅ 已实现 | F3（fuzz_main.cpp） |
| OOM 模拟 | 分配失败注入（rt_alloc failpoint，仅 debug；Windows=夹具层验证 / Linux=链构建深注入） | ✅ 已实现 | F4（fuzz_main.cpp） |
| 半截 .nam | 截断/拼接 .nam 字节流 | ⏳ M4 | — |
| 随机 bytes wav | 随机字节冒充 WAV | ⏳ M3 | — |
| 线程竞态 | 音频线程 vs 加载线程同时改图 | ⏳ 已有并发测试（graph_exchange），fuzz 化 M5 | — |
| 备份损坏 | 截断临时文件/旋转边界/全损坏恢复 | ⏳ 已有单测（integration），fuzz 化 M5 | — |
| 掉电写撕裂模拟 | kill -9/断电点注入 | ⏳ M5 | — |
| 仲裁交错 | CC+UI+场景并发/hang-time 边界 | ⏳ M5（对象未实现） | — |
| 场景 retrigger | 连续快速 recall/淡化中 retrigger/未知参数 ID | ⏳ M5（对象未实现） | — |
| 14-bit CC MSB/LSB 乱序 | 乱序/缺包 | ⏳ M5（对象未实现） | — |

运行方式：`build/debug/tests/fuzz/Debug/namfx_fuzz.exe --iters 1000 --seed 42`（ctest 里跑 300 轮；换种子扩展覆盖面）。

**已知限制（Windows debug）**：注入的 bad_alloc 若落在深层构造路径的容器增长内（链构建/ParamStore/nlohmann 解析），MSVC debug CRT（checked-iterator 代理、容器 backout、RTC 报告）会导致进程 terminate/损坏 —— 已用独立 repro + 栈回溯验证（rt_alloc.cpp 的 OOM-FAIL 日志与 fuzz 的 terminate handler 可复现）。故 Windows 上 F4 只做夹具层注入验证，深注入在 Linux（libstdc++ 异常安全）全量运行。预设路径的崩溃/异常面由 F1/F2 突变 fuzz 覆盖。
