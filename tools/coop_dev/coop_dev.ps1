<#
Quick co-op test: starts a host and a client of Amnesia_coop.exe side by side, connected over localhost,
and drops both straight into a map with no splash screens, menus or lobby clicks.

  .\coop_dev.ps1                                   # 00_rainy_hall at its normal start
  .\coop_dev.ps1 -Map 02_old_archives -Pos PlayerStartArea_1
  .\coop_dev.ps1 -Map 01_old_archives -Debug       # with the game's debug menu (F1)
  .\coop_dev.ps1 -Continue                         # continue together from the host's newest save
  .\coop_dev.ps1 -Scene 02_storage_chase           # both play a scene from _coop\scenes (map and start from the scene)
  .\coop_dev.ps1 -Scene 02_storage_chase -Record   # and film the host's view to _coop\captures\<scene>_host.mp4
  .\coop_dev.ps1 -Kill                             # close both test instances

Uses its own configs in _coop\dev and the test profiles in Documents\Amnesia\CoopLobbyA/B.
The game's own files and configs are not changed. Logs: Documents\Amnesia\CoopLobbyA|B\hpl.log
#>
param(
    [string]$Map = "00_rainy_hall",
    [string]$Pos = "",
    [switch]$Debug,
    [switch]$Kill,
    [switch]$SingleWindow,	# host only, for quick single-player checks of the mod
    [switch]$Continue,		# continue together from the host's newest save instead of a new game
    [string]$Scene = "",	# a scene file in _coop\scenes for the bots to play
    [switch]$Record,		# film the scene (see -RecordSide), 1920x1080
    [ValidateSet("host","client","both")][string]$RecordSide = "host"
)

$ErrorActionPreference = "Stop"
$Game = Split-Path (Split-Path $PSScriptRoot)
$Dev = $PSScriptRoot
$Exe = Join-Path $Game "Amnesia_coop.exe"

$old = Get-Process Amnesia_coop -ErrorAction SilentlyContinue
$old | Stop-Process -Force
$old | Wait-Process -Timeout 15 -ErrorAction SilentlyContinue
if($Kill){ "closed"; return }
Start-Sleep -Milliseconds 500

# A scene says which map and start it is made for
$sceneRel = ""
if($Scene -ne ""){
    $scenePath = if(Test-Path $Scene){ (Resolve-Path $Scene).Path } else { Join-Path $Game "_coop\scenes\$([IO.Path]::ChangeExtension($Scene, '.xml'))" }
    if((Test-Path $scenePath) -eq $false){ throw "Scene $Scene not found" }
    [xml]$sx = Get-Content $scenePath -Raw
    if(-not $PSBoundParameters.ContainsKey("Map") -and $sx.Scene.Map){ $Map = $sx.Scene.Map }
    if(-not $PSBoundParameters.ContainsKey("Pos") -and $sx.Scene.StartPos){ $Pos = $sx.Scene.StartPos }
    if(-not $PSBoundParameters.ContainsKey("RecordSide") -and $sx.Scene.RecordSide){ $RecordSide = $sx.Scene.RecordSide }
    $sceneRel = $scenePath.Substring($Game.Length + 1).Replace("\", "/")
    $sceneName = [IO.Path]::GetFileNameWithoutExtension($scenePath)
    New-Item -ItemType Directory -Force (Join-Path $Game "_coop\captures") | Out-Null
}

$Docs = Join-Path ([Environment]::GetFolderPath("MyDocuments")) "Amnesia"
$Sides = @(
    @{ Key = "A"; Mode = "host"; Folder = "CoopLobbyA"; Name = "Host" },
    @{ Key = "B"; Mode = "client"; Folder = "CoopLobbyB"; Name = "Client" }
)
if($SingleWindow){ $Sides = @($Sides[0]) }

foreach($s in $Sides){
    # Init file: the test one with the requested start map
    $init = Get-Content (Join-Path $Game "_coop\lobbytest\init_$($s.Key).cfg") -Raw
    $mapFile = if($Map.EndsWith(".map")){ $Map } else { "$Map.map" }
    $init = [regex]::Replace($init, '(?s)<StartMap.*?/>', "<StartMap`r`n`tFile = `"$mapFile`"`r`n`tFolder = `"maps/main/`"`r`n`tPos = `"$Pos`"`r`n/>")
    Set-Content -Path (Join-Path $Dev "init_$($s.Key).cfg") -Value $init -Encoding ASCII

    # Co-op settings: connect at start, and the host starts the game once the partner is in
    $coop = "<Player Name=`"$($s.Name)`" />`r`n<Network Mode=`"$($s.Mode)`" Address=`"127.0.0.1`" Port=`"27960`" Transport=`"direct`" />`r`n<Dev AutoStart=`"$(if($Continue){'continue'}else{'true'})`" />`r`n"
    $recording = $Record -and $sceneRel -ne "" -and ($RecordSide -eq "both" -or $RecordSide -eq $s.Mode)
    $s.Recording = $recording
    if($sceneRel -ne ""){
        $devTag = "<Dev AutoStart=`"true`" Scene=`"$sceneRel`""
        if($recording){ $devTag += " Record=`"_coop/captures/$($sceneName)_$($s.Mode).mp4`" RecordStills=`"5`"" }
        $coop = $coop -replace '<Dev [^>]*/>', ($devTag + " />")
    }
    if($SingleWindow){ $coop = "<Player Name=`"$($s.Name)`" />`r`n<Network Mode=`"off`" />`r`n" }
    Set-Content -Path (Join-Path $Dev "coop_$($s.Key).cfg") -Value $coop -Encoding ASCII

    # Test profile's main settings: no splash screens, optional debug menu, no sleeping in the background
    $ms = Join-Path $Docs "$($s.Folder)\main_settings.cfg"
    if(Test-Path $ms){
        $t = Get-Content $ms -Raw
        $t = $t -replace 'ShowPreMenu="[^"]*"', 'ShowPreMenu="false"'
        $t = $t -replace 'LoadDebugMenu="[^"]*"', ('LoadDebugMenu="' + ($(if($Debug){"true"}else{"false"})) + '"')
        $t = $t -replace 'SleepWhenOutOfFocus="[^"]*"', 'SleepWhenOutOfFocus="false"'
        # The filmed side renders at 1920x1080, the other in a small window
        $size = if($s.Recording){ @(1920, 1080) } else { @(960, 540) }
        $t = $t -replace '(<Screen[^>]*?Width=")[^"]*', "`${1}$($size[0])"
        $t = $t -replace '(<Screen[^>]*?Height=")[^"]*', "`${1}$($size[1])"
        $t = $t -replace '(<Screen[^>]*?FullScreen=")[^"]*', '${1}false'
        Set-Content -Path $ms -Value $t -Encoding ASCII -NoNewline
    }

    # Filming: no hint texts or crosshair in the test profile
    $us = Join-Path $Docs "$($s.Folder)\dev_user\user_settings.cfg"
    if((Test-Path $us) -eq $false -and (Test-Path (Split-Path $us))){
        #The game only writes it on a clean exit, and test runs are killed
        Copy-Item (Join-Path $Game "config\default_user_settings.cfg") $us
    }
    if(Test-Path $us){
        $show = if($Record){ "false" } else { "true" }
        $u = Get-Content $us -Raw
        $u = $u -replace ' (ShowHints|ShowCrossHair)="[^"]*"', ''
        $u = $u -replace '<Game', "<Game ShowHints=`"$show`" ShowCrossHair=`"$show`""
        Set-Content -Path $us -Value $u -Encoding ASCII -NoNewline
    }
}

Add-Type @"
using System; using System.Runtime.InteropServices;
public class CoopDevWin { [DllImport("user32.dll")] public static extern bool SetProcessDPIAware(); [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int w,int hh,uint flags); }
"@
[CoopDevWin]::SetProcessDPIAware() | Out-Null

$procs = @()
foreach($s in $Sides){
    $env:AMNESIA_COOP_CFG = Join-Path $Dev "coop_$($s.Key).cfg"
    $procs += Start-Process $Exe -ArgumentList "_coop/dev/init_$($s.Key).cfg" -WorkingDirectory $Game -PassThru
    Start-Sleep -Seconds 3
}

# Side by side on the screen, host left, client right; the windows keep the size the game gave them
Add-Type -AssemblyName System.Windows.Forms
$w = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
$half = [int]($w.Width / 2)
for($i = 0; $i -lt $procs.Count; $i++){
    $p = $procs[$i]
    for($t = 0; $t -lt 40 -and $p.MainWindowHandle -eq 0; $t++){ Start-Sleep -Milliseconds 250; $p.Refresh() }
    [CoopDevWin]::SetWindowPos($p.MainWindowHandle, [IntPtr]::Zero, $w.X + $half * $i, $w.Y, 0, 0, 0x0001 -bor 0x0004) | Out-Null	# NOSIZE, NOZORDER
}
"started: " + (($procs | ForEach-Object { $_.Id }) -join ", ") + "  map $mapFile $Pos" + $(if($sceneRel){ "  scene $sceneRel" + $(if($Record){ " (recording $RecordSide)" }) })
