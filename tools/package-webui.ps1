# NAMFX WebUI shell release packaging (WebView2 + native audio)
# Assembles a self-contained folder:
#   build/namfx-webui-release/
#     NAMFX.exe
#     WebView2Loader.dll
#     www/                 (the WebUI: index/css/js/fonts/presets/models/irs/wasm)
#     presets-demo/        (demo presets, found next to the exe at runtime)
#     README.txt
# User data (saved presets, imports) lives in 文档\namfx as usual.

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$cmake = 'D:\study\vc\vsiualstudio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$build = Join-Path $root 'build'
$exe = Join-Path $build 'release\desktop\namfx_webview_artefacts\Release\NAMFX.exe'
$out = Join-Path $build 'namfx-webui-release'

Write-Host '== building Release shell =='
& $cmake --build (Join-Path $build 'release') --config Release --target namfx_webview
if (-not (Test-Path $exe)) {
    throw 'Release build failed: NAMFX.exe not found'
}

Write-Host '== assembling package =='
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Path $out | Out-Null

Copy-Item $exe (Join-Path $out 'NAMFX.exe')
Copy-Item (Join-Path (Split-Path $exe) 'WebView2Loader.dll') (Join-Path $out 'WebView2Loader.dll')
Copy-Item (Join-Path $root 'webui\www') (Join-Path $out 'www') -Recurse
Copy-Item (Join-Path $root 'core\preset\demo') (Join-Path $out 'presets-demo') -Recurse
New-Item -ItemType Directory -Path (Join-Path $out 'models') | Out-Null
New-Item -ItemType Directory -Path (Join-Path $out 'irs') | Out-Null
foreach ($source in @((Join-Path $root 'modles\nam'), (Join-Path $root 'nam'))) {
    if (Test-Path $source) {
        Get-ChildItem -LiteralPath $source -Recurse -File -Filter '*.nam' | Copy-Item -Destination (Join-Path $out 'models')
    }
}
foreach ($source in @((Join-Path $root 'modles\ir'), (Join-Path $root 'ir'))) {
    if (Test-Path $source) {
        Get-ChildItem -LiteralPath $source -Recurse -File -Filter '*.wav' | Copy-Item -Destination (Join-Path $out 'irs')
    }
}

Copy-Item (Join-Path $root 'webui\README.md') (Join-Path $out 'README.txt')

Write-Host "== done: $out =="
