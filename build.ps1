[CmdletBinding()]
param(
    [string]$BuildDirectory,
    [string]$Generator = 'Visual Studio 18 2026',
    [string[]]$CMakeArguments = @(),
    [switch]$Package,
    [string]$PackageVersion
)
$ErrorActionPreference = 'Stop'
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $PSScriptRoot 'build' }
. (Join-Path $PSScriptRoot 'cmake/version.ps1')
$PackageVersion = (Get-MakiBuildVersion $PackageVersion).Name
function Invoke-CMake([string[]]$Arguments) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & cmake @Arguments 2>&1 | ForEach-Object { $_.ToString() }
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previous }
    if ($code -ne 0) { throw "CMake failed ($code): $Arguments" }
}
Invoke-CMake (@('-S',$PSScriptRoot,'-B',$BuildDirectory,'-G',$Generator,'-A','Win32') + $CMakeArguments +
    @("-DTTP_MAKI_BUILD_VERSION=$PackageVersion"))
Invoke-CMake @('--build',$BuildDirectory,'--config','Release','--target','ttp_maki','--parallel','4')
Assert-MakiFileVersion (Join-Path $BuildDirectory 'Release/ttp_maki.dll') $PackageVersion
if (-not $Package) { return }
$output = Join-Path $BuildDirectory 'Release'
$stage = Join-Path $BuildDirectory ('package-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Invoke-CMake @('--install',$BuildDirectory,'--config','Release','--component','Runtime','--prefix',$stage)
$hash = (Get-FileHash -LiteralPath (Join-Path $stage 'AddIn/ttp_maki.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  AddIn/ttp_maki.dll" | Set-Content -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Encoding UTF8
$archive = Join-Path $output "ttp_maki-$PackageVersion.zip"
Compress-Archive -LiteralPath (Join-Path $stage 'AddIn'),(Join-Path $stage 'SHA256SUMS.txt') -DestinationPath $archive -Force
$archiveHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$archiveHash  ttp_maki-$PackageVersion.zip" | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Encoding UTF8
Write-Output "MAKI package: $archive"
