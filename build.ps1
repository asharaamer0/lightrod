param([switch]$RenderTest)
$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
$buildDir = Join-Path $taskRoot 'build'
$sourceDir = Join-Path $taskRoot 'fusion-main'
$portableBin = Join-Path $taskRoot '.tools\w64devkit\bin'
$previousPath = $env:PATH
try {
    if (Test-Path (Join-Path $portableBin 'g++.exe')) {
        $env:PATH = "$portableBin;$env:PATH"
        & cmake -S $sourceDir -B $buildDir -G 'MinGW Makefiles' '-DCMAKE_MAKE_PROGRAM=mingw32-make' '-DCMAKE_C_COMPILER=gcc' '-DCMAKE_CXX_COMPILER=g++' -DCMAKE_BUILD_TYPE=Release
    } else {
        & cmake -S $sourceDir -B $buildDir -A x64
    }
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & cmake --build $buildDir --config Release --parallel 6
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
    & ctest --test-dir $buildDir -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Behavior tests failed.' }
    $dll = Join-Path $buildDir 'Lightrod.dll'
    $renderExe = Join-Path $buildDir 'lightrod_menu_smoke.exe'
    $injectorExe = Join-Path $buildDir 'Lightrod-Injector.exe'
    if (-not (Test-Path $dll)) {
        $dll = Join-Path $buildDir 'Release\Lightrod.dll'
        $renderExe = Join-Path $buildDir 'Release\lightrod_menu_smoke.exe'
        $injectorExe = Join-Path $buildDir 'Release\Lightrod-Injector.exe'
    }
    if ($RenderTest) {
        $jvmFile = Get-ChildItem -LiteralPath (Join-Path $env:USERPROFILE '.lunarclient\jre') -Filter jvm.dll -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $jvmFile) {
            $jvmFile = Get-ChildItem -LiteralPath 'C:\Program Files\Eclipse Adoptium' -Filter jvm.dll -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
        }
        if (-not $jvmFile) { throw 'Render test requires a local x64 JVM (jvm.dll).' }
        $jvmDir = $jvmFile.DirectoryName
        $env:PATH = "$jvmDir;$(Split-Path $jvmDir -Parent);$env:PATH"
        & $renderExe (Join-Path $buildDir 'menu-preview.bmp')
        if ($LASTEXITCODE -ne 0) { throw "Render test failed: $LASTEXITCODE" }
        Add-Type -AssemblyName System.Drawing
        $preview = [System.Drawing.Image]::FromFile((Join-Path $buildDir 'menu-preview.bmp'))
        try { $preview.Save((Join-Path $buildDir 'menu-preview.png'), [System.Drawing.Imaging.ImageFormat]::Png) }
        finally { $preview.Dispose() }
    }
    $distributionDir = Join-Path $taskRoot 'dist'
    New-Item -ItemType Directory -Path $distributionDir -Force | Out-Null
    Copy-Item -LiteralPath $dll -Destination (Join-Path $distributionDir 'Lightrod.dll') -Force
    Copy-Item -LiteralPath $injectorExe -Destination (Join-Path $distributionDir 'Lightrod-Injector.exe') -Force
    Copy-Item -LiteralPath (Join-Path $sourceDir 'LICENSE') -Destination (Join-Path $distributionDir 'LICENSE') -Force
    Copy-Item -LiteralPath (Join-Path $taskRoot 'README.md') -Destination (Join-Path $distributionDir 'README.md') -Force
    $packageFiles = @('Lightrod-Injector.exe', 'Lightrod.dll', 'README.md', 'LICENSE') | ForEach-Object { Join-Path $distributionDir $_ }
    Compress-Archive -LiteralPath $packageFiles -DestinationPath (Join-Path $distributionDir 'Lightrod.zip') -Force
    Write-Output "Built and tested: $(Join-Path $distributionDir 'Lightrod-Injector.exe')"
    Write-Output "Bundle: $(Join-Path $distributionDir 'Lightrod.zip')"
} finally { $env:PATH = $previousPath }
