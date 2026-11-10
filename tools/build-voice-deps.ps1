# Builds the native voice DLLs copied into third_party/voice.
# Requires: Visual Studio 2022 (MSVC), CMake, Git, network access to github.com/microsoft/vcpkg.
param(
    [string]$VcpkgDir = (Join-Path $PSScriptRoot "..\third_party\libdave\cpp\vcpkg"),
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\third_party\libdave\cpp\build-native"),
    [string]$OutDir = (Join-Path $PSScriptRoot "..\third_party\voice")
)
$ErrorActionPreference = "Stop"
$libdave = Resolve-Path (Join-Path $PSScriptRoot "..\third_party\libdave")
$cmake = "C:\Program Files\CMake\bin\cmake.exe"
if (-not (Test-Path $cmake)) { $cmake = "cmake" }

if (-not (Test-Path (Join-Path $VcpkgDir "vcpkg.exe"))) {
    if (-not (Test-Path (Join-Path $VcpkgDir ".git"))) {
        git clone --depth 1 https://github.com/microsoft/vcpkg.git $VcpkgDir
    }
    # Manifests pin baseline commits, which a shallow clone cannot resolve.
    if ((git -C $VcpkgDir rev-parse --is-shallow-repository) -eq "true") {
        git -C $VcpkgDir fetch --unshallow
    }
    & (Join-Path $VcpkgDir "bootstrap-vcpkg.bat") -disableMetrics
}

$manifest = Join-Path $libdave "cpp\vcpkg-alts\openssl_3"
$toolchain = Join-Path $VcpkgDir "scripts\buildsystems\vcpkg.cmake"
& $cmake -S (Join-Path $libdave "cpp") -B $BuildDir -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded `
    "-DVCPKG_MANIFEST_DIR=$manifest" `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
    -DVCPKG_TARGET_TRIPLET=x64-windows-static -DVCPKG_TARGET_ARCHITECTURE=x86_64 `
    -DBUILD_SHARED_LIBS=ON -DENABLE_SANITIZERS=OFF -DPERSISTENT_KEYS=OFF -DTESTING=OFF -DFUZZER=OFF
if ($LASTEXITCODE -ne 0) { throw "libdave configure failed" }

& $cmake --build $BuildDir --target libdave --config Release
if ($LASTEXITCODE -ne 0) { throw "libdave build failed" }

$classic = Join-Path ([System.IO.Path]::GetTempPath()) "rea-vcpkg-classic"
New-Item -ItemType Directory -Force -Path $classic | Out-Null
Push-Location $classic
try {
    & (Join-Path $VcpkgDir "vcpkg.exe") install "libsodium:x64-windows" "opus:x64-windows" --disable-metrics
    if ($LASTEXITCODE -ne 0) { throw "vcpkg install failed" }
} finally { Pop-Location }

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Copy-Item (Join-Path $BuildDir "Release\libdave.dll") $OutDir -Force
Copy-Item (Join-Path $VcpkgDir "installed\x64-windows\bin\libsodium.dll") $OutDir -Force
Copy-Item (Join-Path $VcpkgDir "installed\x64-windows\bin\opus.dll") $OutDir -Force
Copy-Item (Join-Path $libdave "cpp\includes\dave\dave.h") $OutDir -Force
Get-ChildItem $OutDir -Filter *.dll | ForEach-Object {
    "{0}  {1}  {2}" -f $_.Name, $_.Length, (Get-FileHash $_.FullName -Algorithm SHA256).Hash
}
