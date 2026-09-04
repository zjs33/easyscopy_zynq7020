$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$cmake = 'D:\qt\Tools\CMake_64\bin\cmake.exe'
$ninja = 'D:\qt\Tools\Ninja\ninja.exe'
$qtPrefix = 'D:\qt\6.11.1\mingw_64'
$cCompiler = 'D:\qt\Tools\mingw1310_64\bin\gcc.exe'
$cxxCompiler = 'D:\qt\Tools\mingw1310_64\bin\g++.exe'
$windeployqt = 'D:\qt\6.11.1\mingw_64\bin\windeployqt.exe'
$buildDir = Join-Path $projectRoot 'build-release'

& $cmake -S $projectRoot -B $buildDir -G Ninja `
    "-DCMAKE_BUILD_TYPE=Release" `
    "-DCMAKE_PREFIX_PATH=$qtPrefix" `
    "-DCMAKE_C_COMPILER=$cCompiler" `
    "-DCMAKE_CXX_COMPILER=$cxxCompiler" `
    "-DCMAKE_MAKE_PROGRAM=$ninja"

& $cmake --build $buildDir --parallel
& $windeployqt --release --compiler-runtime --no-translations `
    (Join-Path $buildDir 'src\ac880_zynq_scope.exe')
& $cmake --build $buildDir --target test
& ctest --test-dir $buildDir --output-on-failure
