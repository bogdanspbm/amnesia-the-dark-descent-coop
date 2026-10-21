<#
Packs the Windows release zip: Amnesia_coop.exe, the partner model, coop.cfg.example and install notes.
Only the mod's own files go in; nothing from the game install and no Steamworks SDK files.

  .\build\package_windows.ps1 -Version 0.1.0          # after build_windows.ps1; writes dist\amnesia-coop-0.1.0-windows.zip
#>
param(
    [Parameter(Mandatory=$true)][string]$Version,
    [string]$Exe = "",
    [string]$OutDir = ""
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

$bad = Get-ChildItem -Recurse $Stage -Include steam_api*.dll, *.lib, *.pdb
if($bad){ throw "Refusing to package: $($bad.FullName -join ', ')" }

$Zip = "$Stage.zip"
if(Test-Path $Zip){ Remove-Item $Zip }
Compress-Archive -Path "$Stage\*" -DestinationPath $Zip
Get-Item $Zip
