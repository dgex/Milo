param(
    [string]$SdkRoot = 'C:/Emb_Dev_Projects/NXP_SDK/evkbmimxrt1170_26.06.00_LTS/mcuxsdk',
    [ValidateSet('flexspi_nor_debug','flexspi_nor_release')]
    [string]$Configuration = 'flexspi_nor_debug',
    [string]$ToolsRoot = "$env:USERPROFILE/.mcuxpressotools"
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$sdkWorkspace = Split-Path $SdkRoot -Parent
$venv = Join-Path $ToolsRoot '.mcux-venv-3.12/Scripts'
$toolchain = Join-Path $ToolsRoot 'arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi'
$cmake = Join-Path $ToolsRoot 'cmake-3.30.0-windows-x86_64/bin'
$ninja = Join-Path $ToolsRoot 'ninja-1.12.1'
foreach ($required in @("$SdkRoot/MCUX_VERSION", "$venv/west.exe", "$toolchain/bin/arm-none-eabi-gcc.exe", "$cmake/cmake.exe", "$ninja/ninja.exe")) {
    if (!(Test-Path -LiteralPath $required)) { throw "Missing dependency: $required" }
}
$originalPath = $env:PATH
$originalArm = $env:ARMGCC_DIR
try {
    $env:PATH = "$venv;$cmake;$ninja;$toolchain/bin;$originalPath"
    $env:ARMGCC_DIR = $toolchain
    Push-Location $sdkWorkspace
    try {
        & "$venv/west.exe" build -b evkbmimxrt1170 -d "$projectRoot/build/$Configuration" "$projectRoot/firmware" --toolchain armgcc --config $Configuration -- -Dcore_id=cm7 "-DSdkRootDirPath=$SdkRoot"
        if ($LASTEXITCODE -ne 0) { throw "Firmware build failed (exit $LASTEXITCODE)." }
    } finally { Pop-Location }
} finally {
    $env:PATH = $originalPath
    $env:ARMGCC_DIR = $originalArm
}
