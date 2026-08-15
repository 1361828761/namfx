# NAMFX desktop release packaging
# Builds Release and assembles a self-contained folder:
#   build/namfx-release/
#     NAM Editor.exe
#     presets/            (demo presets, found next to the exe at runtime)
#     README.txt
# User models/IRs stay personal: import them via the editor (Import button)
# or drop .nam files into 文档\namfx\models.

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$cmake = 'D:\study\vc\vsiualstudio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$build = Join-Path $root 'build'
$exe = Join-Path $build 'release\desktop\namfx_editor_artefacts\Release\NAM Editor.exe'
$out = Join-Path $build 'namfx-release'

Write-Host '== building Release =='
& $cmake --build (Join-Path $build 'release') --config Release --target namfx_editor
if (-not (Test-Path $exe)) {
    throw 'Release build failed: NAM Editor.exe not found'
}

Write-Host '== assembling package =='
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Path $out | Out-Null

Copy-Item $exe (Join-Path $out 'NAM Editor.exe')
Copy-Item (Join-Path $root 'core\preset\demo') (Join-Path $out 'presets') -Recurse

$readme = @'
NAMFX - 智能电吉他综合效果器（桌面版）
========================================

运行：双击 NAM Editor.exe

功能：
  - 预设加载/保存/删除（用户预设存于 文档\namfx\presets）
  - 音色链编辑：添加/删除/参数/旁通/拖拽重排
  - NAM 模型库：Import 按钮导入 .nam（存于 文档\namfx\models），
    也可直接把 .nam 丢进该目录；D:/download/NsTone/nam 自动扫描
  - IR 箱体：cab.ir 选择 IR（文档\namfx\irs 或 exe 旁 irs）
  - 场景：8 场景切换/保存，可绑 MIDI CC
  - MIDI 学习：参数行 Lrn / 场景 Lrn CC，绑定持久化于
    文档\namfx\midi_binds.txt
  - 音频设备：WASAPI（shared/exclusive/low-latency）/ ASIO（ASIO4ALL 等）
  - 调音器：图形仪表 + 自动识别弦（EADGBE / Drop D）

提示：
  - 低延迟：设备类型选 ASIO（或 Windows Audio low latency），块 128
  - 若 GE100 输出带效果，先在其上选清音预设（USB 输出 = 效果器总输出）
'@
[System.IO.File]::WriteAllText((Join-Path $out 'README.txt'), $readme, [System.Text.Encoding]::UTF8)

Write-Host "== done: $out =="
