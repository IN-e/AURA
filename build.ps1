$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$buildDir = Join-Path $root 'build'
$objDir = Join-Path $buildDir 'obj'
$distDir = Join-Path $root 'dist'
New-Item -ItemType Directory -Force -Path $buildDir, $objDir, $distDir | Out-Null

if (-not (Test-Path (Join-Path $root 'src\aura.ico'))) {
    & (Join-Path $root 'tools\make_icon.ps1')
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = $null
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $vsPath) {
    $known = Join-Path $env:ProgramFiles 'Microsoft Visual Studio\18\Community'
    if (Test-Path $known) { $vsPath = $known }
}
if (-not $vsPath) { throw 'Visual Studio C++ 生成工具未找到。' }
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "找不到 vcvars64.bat: $vcvars" }

$sources = @(
    'src\main.cpp',
    'src\panel.cpp',
    'src\panel_render.cpp',
    'src\overlay.cpp',
    'src\analyzer.cpp',
    'src\audio.cpp',
    'src\gfx.cpp',
    'src\settings.cpp'
)
$libs = @(
    'user32.lib','gdi32.lib','shell32.lib','shcore.lib','d2d1.lib','dwrite.lib',
    'd3d11.lib','dxgi.lib','dcomp.lib','windowscodecs.lib','mmdevapi.lib',
    'ole32.lib','oleaut32.lib','dwmapi.lib','version.lib','propsys.lib','dxguid.lib'
)

Get-ChildItem -Path $objDir -Filter '*.obj' -ErrorAction SilentlyContinue | Remove-Item -Force
Get-ChildItem -Path (Join-Path $buildDir 'app.res') -ErrorAction SilentlyContinue | Remove-Item -Force

$batch = @"
@echo off
setlocal
call "$vcvars" >nul
if errorlevel 1 exit /b 1
cl /nologo /c /std:c++20 /EHsc /utf-8 /O2 /GL /Gy /Gw /W4 /MT /DUNICODE /D_UNICODE /DNOMINMAX /Fo:build\obj\ $($sources -join ' ')
if errorlevel 1 exit /b 1
rc /nologo /I src /fo build\app.res src\app.rc
if errorlevel 1 exit /b 1
link /nologo /OUT:dist\AURA.exe /SUBSYSTEM:WINDOWS /MANIFEST:NO /LTCG /OPT:REF /OPT:ICF /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /guard:cf build\obj\*.obj build\app.res $($libs -join ' ')
if errorlevel 1 exit /b 1
endlocal
"@
$batchPath = Join-Path $buildDir 'compile.bat'
Set-Content -Path $batchPath -Value $batch -Encoding ASCII
Push-Location $root
try {
    & cmd.exe /c $batchPath
    if ($LASTEXITCODE -ne 0) { throw "构建失败，退出码 $LASTEXITCODE" }
} finally {
    Pop-Location
}

$exe = Join-Path $distDir 'AURA.exe'
Get-Item $exe | Select-Object FullName, Length, LastWriteTime
Write-Host '构建完成。'
