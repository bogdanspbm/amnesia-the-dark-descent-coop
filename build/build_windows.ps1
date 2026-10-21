<#
Builds Amnesia_coop.exe (Win32 Release) with Visual Studio 2022 (MSVC v143) and a Windows 10/11 SDK.

  .\build\build_windows.ps1                                         # without Steam lobbies (direct IP only)
  .\build\build_windows.ps1 -SteamworksSdk E:\SteamworksSDK\sdk     # with Steam lobbies

Output: amnesia\redist\Amnesia.exe (copy it into the game folder as Amnesia_coop.exe).
Used by the GitHub Actions workflow too.
#>
param(
    [string]$SteamworksSdk = "",
    [string]$MSBuild = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path $PSScriptRoot

if(-not $MSBuild){
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $MSBuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
    if(-not $MSBuild){ throw "MSBuild not found; install Visual Studio 2022 Build Tools with the C++ workload" }
}

# The engine's prebuilt Win32 dependencies ship in the repository as a zip
$Deps = Join-Path $Root "HPL2\dependencies"
if(-not (Test-Path (Join-Path $Deps "lib\win32"))){
    Write-Host "Extracting HPL2\dependencies.zip"
    Expand-Archive -Path (Join-Path $Root "HPL2\dependencies.zip") -DestinationPath $Deps -Force
}

$common = @("/p:Configuration=Release", "/p:Platform=Win32", "/p:PlatformToolset=v143",
            "/p:WindowsTargetPlatformVersion=10.0", "/m", "/nologo", "/v:minimal")

Write-Host "Building HPL2 engine"
& $MSBuild (Join-Path $Root "HPL2\core\_HPL2_2010.vcxproj") @common
if($LASTEXITCODE -ne 0){ throw "HPL2 build failed" }

$lux = @()
if($SteamworksSdk){
    $sdk = (Resolve-Path $SteamworksSdk).Path.TrimEnd('\') + '\'
    if(-not (Test-Path "$sdk\public\steam\steam_api.h")){ throw "$sdk is not the Steamworks SDK 'sdk' folder" }
    $lux += "/p:SteamworksSdkDir=$sdk"
    Write-Host "Building Amnesia (co-op) with Steam lobbies"
} else {
    Write-Host "Building Amnesia (co-op) without Steam lobbies"
}
& $MSBuild (Join-Path $Root "amnesia\src\game\Lux.vcxproj") @common @lux
if($LASTEXITCODE -ne 0){ throw "Amnesia build failed" }

Get-Item (Join-Path $Root "amnesia\redist\Amnesia.exe")
