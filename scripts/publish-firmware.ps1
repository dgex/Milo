param(
    [Parameter(Mandatory=$true)][string]$ValidationNotes,
    [switch]$Push,
    [string]$ToolsRoot = "$env:USERPROFILE/.mcuxpressotools"
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
function Invoke-RepoGit {
    $result = & git -c "safe.directory=$($root.Replace('\','/'))" -C $root @args
    if ($LASTEXITCODE -ne 0) { throw "Git failed: $args" }
    return $result
}
# Publish committed source only, so the manifest identifies a reproducible source revision.
if (Invoke-RepoGit status --porcelain -- firmware src scripts) {
    throw 'Commit firmware, src, and scripts changes before publishing.'
}
$sourceCommit = Invoke-RepoGit rev-parse HEAD
$release = Join-Path $root 'build/flexspi_nor_release'
$elf = Join-Path $release 'rt1170_companion_cm7.elf'
$bin = Join-Path $release 'rt1170_companion.bin'
foreach ($path in @($elf,$bin)) {
    if (!(Test-Path -LiteralPath $path)) { throw "Missing release artifact: $path" }
}
$builtAt = (Get-Item -LiteralPath $elf).LastWriteTimeUtc
foreach ($relative in (Invoke-RepoGit ls-files firmware src scripts)) {
    if ((Get-Item -LiteralPath (Join-Path $root $relative)).LastWriteTimeUtc -gt $builtAt) {
        throw "Build is older than $relative. Build release again and validate it first."
    }
}
$objcopy = Join-Path $ToolsRoot 'arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi/bin/arm-none-eabi-objcopy.exe'
$comparison = Join-Path $release 'publish-verify.bin'
& $objcopy -O binary $elf $comparison
if ($LASTEXITCODE -ne 0) { throw 'ELF conversion failed.' }
if ((Get-FileHash $comparison).Hash -ne (Get-FileHash $bin).Hash) { throw 'BIN does not match ELF.' }
# Current linker puts the first loadable flash section at 0x30000400.
$readelf = Join-Path $ToolsRoot 'arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi/bin/arm-none-eabi-readelf.exe'
$sections = & $readelf -SW $elf
if ($LASTEXITCODE -ne 0 -or !($sections -match '\.flash_config\s+PROGBITS\s+30000400')) {
    throw 'Unexpected flash layout; review the binary base address before publishing.'
}
$destination = Join-Path $root 'releases/latest'
New-Item -ItemType Directory -Force $destination | Out-Null
Copy-Item -LiteralPath $elf -Destination (Join-Path $destination 'Milo.elf')
Copy-Item -LiteralPath $bin -Destination (Join-Path $destination 'Milo.bin')
$hashes = @{}
foreach ($name in @('Milo.elf','Milo.bin')) { $hashes[$name] = (Get-FileHash (Join-Path $destination $name) -Algorithm SHA256).Hash.ToLower() }
[ordered]@{
    source_commit = $sourceCommit
    board = 'MIMXRT1170-EVKB'
    configuration = 'flexspi_nor_release'
    build_time_utc = $builtAt.ToString('o')
    published_time_utc = [DateTime]::UtcNow.ToString('o')
    bin_address = '0x30000400'
    validation = $ValidationNotes
    sha256 = $hashes
} | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $destination 'manifest.json')
$hashes.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Value)  $($_.Key)" } | Set-Content (Join-Path $destination 'SHA256SUMS.txt')
if ($Push) {
    Invoke-RepoGit add -- releases/latest
    Invoke-RepoGit commit -m "Publish validated Milo firmware from $($sourceCommit.Substring(0,7))"
    Invoke-RepoGit push
}
Write-Output "Release artifacts ready at $destination"
