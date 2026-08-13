# AGENTS.md

namfx 执行环境的精简版速查。完整版见 `docs/EXECUTION.md`。

## 环境

| 项 | 要求 |
|---|---|
| 编译器 | MSVC 2019+（Windows）/ GCC 9+ 或 Clang（Linux/macOS） |
| CMake | ≥ 3.24（用 `CMakePresets.json`，`cmake --preset <name>`） |
| C++ 标准 | C++17（core 必须保持 17，不得升级） |
| 测试框架 | Catch2 v3（`tests/unit/` 用 FetchContent 引入，仅测试目标依赖） |
| core 依赖 | 音频路径零第三方运行时依赖（只允许 std::）；预设加载路径允许 header-only 豁免（nlohmann/json） |

## 工程红线（违反 = 返工）

1. 音频线程实时安全：回调内零堆分配 / 零锁 / 零 I/O / 零异常
2. core 音频路径零第三方依赖；JUCE 只出现在 `desktop/`；SIMD 选项只允许出现在 `core/modules/nam/` 的 NAM 库 target
3. 图交换协议：所有音频图变更 = request → 后台加载 → 双缓冲原子交换；UI 线程直接改图 = 错误
4. 模块 ID 全局唯一；预设加载逐槽显式校验并报错，不静默失败
5. 引擎是参数权威：UI 只读镜像，经队列订阅引擎广播
6. 术语纪律：写代码/文档前查 `CONTEXT.md`
7. 预设 schema 改动必须走迁移链
8. 测试先行：每个模块 = 单元 + 回归 + 参数空间扫描

## 已知坑

| 坑 | 对策 |
|---|---|
| 本机 GitHub 直连被重置、代理 ~150KB/s，FetchContent 大克隆会卡死 | 依赖已本地化：`FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=D:/study/project/nam/build/nlohmann-v3.11.3`（configure 时 -D 传入，已持久化在 cache；新建 build 目录需重传）+ 项目已设 `UPDATE_DISCONNECTED`；新依赖（M4 NAM Core）同法：代理下载 tarball → 解压 → SOURCE_DIR 覆盖。勿中途 kill configure（会产生僵尸 git 锁死 _deps 目录） |
| NAM lazy init 预热前回调内分配 | 加载后后台跑哑输入预热 + 分配器检测 |
| 持久化写盘断电损坏 | temp → fsync → rename → fsync(dir) → 读回校验（Windows 用 fopen("wb")+_commit(_fileno)，_commit 对只读句柄报 EBADF） |
| 关机 pop | 掉电检测 + 储能窗口内 mute 序列 |
| CC 7-bit 量化感 | 14-bit MSB/LSB + 每源每参数平滑 |
| A2 训练采样率固定 96k | 内部重采样或 48k 域处理（M4 定） |

## 不做

- 不 commit（orchestrator 负责）
- 不改 `docs/PLAN.md`、`docs/EXECUTION.md`、`CONTEXT.md`、`TODOS.md`
- 不加代码注释（除非必要）
