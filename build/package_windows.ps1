<#
Packs the Windows release zip: Amnesia_coop.exe, the partner model, coop.cfg.example and install notes.
Only the mod's own files go in, nothing from the game install. With -SteamDll the Steamworks SDK's
redistributable steam_api.dll goes into _coop\steam (the release build, so Steam lobbies work out of the box);
it is never committed to git.

  .\build\package_windows.ps1 -Version 0.1.0 -SteamDll <sdk>\redistributable_bin\steam_api.dll
#>
param(
    [Parameter(Mandatory=$true)][string]$Version,
    [string]$Exe = "",
    [string]$OutDir = "",
    [string]$SteamDll = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path $PSScriptRoot
if(-not $Exe){ $Exe = Join-Path $Root "amnesia\redist\Amnesia.exe" }
if(-not $OutDir){ $OutDir = Join-Path $Root "dist" }

$Stage = Join-Path $OutDir "amnesia-coop-$Version-windows"
if(Test-Path $Stage){ Remove-Item -Recurse -Force $Stage }
New-Item -ItemType Directory -Force "$Stage\_coop\partner", "$Stage\_coop\steam" | Out-Null

Copy-Item $Exe "$Stage\Amnesia_coop.exe"
Copy-Item (Join-Path $Root "coop_data\partner\*") "$Stage\_coop\partner\"
Copy-Item (Join-Path $Root "amnesia\src\coop\coop.cfg.example") $Stage
Copy-Item (Join-Path $PSScriptRoot "package\INSTALL.txt") $Stage
Copy-Item (Join-Path $PSScriptRoot "package\steam_README.txt") "$Stage\_coop\steam\README.txt"
Copy-Item (Join-Path $Root "LICENSE") "$Stage\LICENSE.txt"
Copy-Item (Join-Path $Root "CREDITS.md") "$Stage\CREDITS.md"

if($SteamDll){
    Copy-Item $SteamDll "$Stage\_coop\steam\steam_api.dll"
    Remove-Item "$Stage\_coop\steam\README.txt"
}

$bad = Get-ChildItem -Recurse $Stage -Include steam_api64.dll, *.lib, *.pdb
if($bad){ throw "Refusing to package: $($bad.FullName -join ', ')" }

$Zip = "$Stage.zip"
if(Test-Path $Zip){ Remove-Item $Zip }
Compress-Archive -Path "$Stage\*" -DestinationPath $Zip
Get-Item $Zip
