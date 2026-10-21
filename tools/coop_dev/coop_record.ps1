<#
Settings for recording co-op footage (eg with Win+Alt+R): 1920x1080 fullscreen, VSync on,
no hints, no crosshair, the simple focus icons (the game has no setting to hide them). Run it before recording and with -Restore afterwards.

  .\coop_record.ps1            # back up the settings and switch to recording settings
  .\coop_record.ps1 -Restore   # put the backed-up settings back
  .\coop_record.ps1 -Root <dir># another save folder than Documents\Amnesia\Main (used for testing)

Only the player's own settings in the save folder are changed; the game's files are not.
A file that did not exist yet is created from the game's defaults and removed again by -Restore.
#>
param(
    [switch]$Restore,
    [string]$Root = (Join-Path ([Environment]::GetFolderPath("MyDocuments")) "Amnesia\Main"),
    [int]$Width = 1920,
    [int]$Height = 1080
)

$ErrorActionPreference = "Stop"
$Game = Split-Path (Split-Path $PSScriptRoot)
$Backup = Join-Path $PSScriptRoot ("backup\" + ($Root -replace '[:\\/]', '_'))
$Manifest = Join-Path $Backup "files.txt"	# one line per file: <relative path>|existed or new

if($Restore){
    if((Test-Path $Manifest) -eq $false){ "No backup to restore in $Backup"; return }
    foreach($line in Get-Content $Manifest){
        $rel, $state = $line -split '\|'
        $target = Join-Path $Root $rel
        if($state -eq "existed"){ Copy-Item (Join-Path $Backup $rel) $target -Force; "restored  $rel" }
        elseif(Test-Path $target){ Remove-Item $target; "removed   $rel (did not exist before)" }
    }
    Remove-Item $Backup -Recurse
    return
}

if(Test-Path $Manifest){ "Recording settings are already on (backup in $Backup). Run with -Restore first."; return }
if((Test-Path $Root) -eq $false){ "Save folder $Root not found. Start the game once first."; return }

New-Item -ItemType Directory -Force $Backup | Out-Null
$entries = @()
function Prepare($rel, $default){
    $target = Join-Path $Root $rel
    if(Test-Path $target){
        $dst = Join-Path $Backup $rel
        New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
        Copy-Item $target $dst -Force
        $script:entries += "$rel|existed"
    } else {
        Copy-Item (Join-Path $Game $default) $target
        $script:entries += "$rel|new"
    }
    return $target
}
function SetAttr($text, $elem, $attr, $value){
    $rx = "(<$elem\b[^>]*?\b$attr=`")[^`"]*(`")"
    if($text -match $rx){ return [regex]::Replace($text, $rx, "`${1}$value`${2}") }
    return [regex]::Replace($text, "<$elem\b", "<$elem $attr=`"$value`"", 1)	# attribute missing: add it
}

# Main settings: resolution, fullscreen, vsync
$ms = Prepare "main_settings.cfg" "config\default_main_settings_sdl2.cfg"
$t = Get-Content $ms -Raw
foreach($kv in @(@("Width", $Width), @("Height", $Height), @("FullScreen", "true"), @("Vsync", "true"))){ $t = SetAttr $t "Screen" $kv[0] $kv[1] }
Set-Content $ms $t -Encoding ASCII -NoNewline
"main_settings.cfg: ${Width}x${Height}, fullscreen, vsync"

# Every profile: no hints, crosshair or focus icons (subtitles stay)
$profiles = Get-ChildItem $Root -Directory
foreach($p in $profiles){
    $us = Prepare "$($p.Name)\user_settings.cfg" "config\default_user_settings.cfg"
    $t = Get-Content $us -Raw
    foreach($kv in @(@("ShowHints", "false"), @("ShowDeathHints", "false"), @("ShowCrossHair", "false"), @("FocusIconStyle", "Simple"))){ $t = SetAttr $t "Game" $kv[0] $kv[1] }
    Set-Content $us $t -Encoding ASCII -NoNewline
    "$($p.Name)\user_settings.cfg: no hints or crosshair, simple focus icons"
}

Set-Content $Manifest $entries -Encoding ASCII
"Backup in $Backup. Afterwards run: coop_record.ps1 -Restore"
