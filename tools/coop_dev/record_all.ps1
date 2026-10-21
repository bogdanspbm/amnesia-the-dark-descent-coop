<#
Films every scene in _coop\scenes one after the other (host's view, 1920x1080) and makes a contact sheet for each.

  .\record_all.ps1                         # all scenes
  .\record_all.ps1 -Scenes 02_storage_chase
#>
param([string[]]$Scenes = @(), [int]$TimeOut = 240)

$Game = Split-Path (Split-Path $PSScriptRoot)
if($Scenes.Count -eq 0){ $Scenes = Get-ChildItem (Join-Path $Game "_coop\scenes") -Filter *.xml | ForEach-Object { $_.BaseName } }
$log = Join-Path ([Environment]::GetFolderPath("MyDocuments")) "Amnesia\CoopLobbyA\hpl.log"

foreach($scene in $Scenes){
    [xml]$sx = Get-Content (Join-Path $Game "_coop\scenes\$scene.xml") -Raw
    $side = if($sx.Scene.RecordSide){ $sx.Scene.RecordSide } else { "host" }
    $video = Join-Path $Game "_coop\captures\$($scene)_$side.mp4"
    $frames = [IO.Path]::ChangeExtension($video, $null).TrimEnd('.') + "_frames"
    if(Test-Path $frames){ Remove-Item -LiteralPath $frames -Recurse }

    & (Join-Path $PSScriptRoot "coop_dev.ps1") -Scene $scene -Record | Out-Host
    Start-Sleep 5
    $start = Get-Date
    $log = Join-Path ([Environment]::GetFolderPath("MyDocuments")) ("Amnesia\" + $(if($side -eq "client"){ "CoopLobbyB" } else { "CoopLobbyA" }) + "\hpl.log")
    while(((Get-Date) - $start).TotalSeconds -lt $TimeOut){
        if((Get-Content $log -ErrorAction SilentlyContinue) -match "Coop: recording finished"){ break }
        Start-Sleep 2
    }
    $line = (Get-Content $log) -match "Coop: recording finished|Coop: recording \d"
    "$scene : " + ($line -join " | ")
    # The recorder unmutes the game in the Windows mixer for the take and puts it back when it stops;
    # killed before that, the mixer would stay unmuted
    $text = Get-Content $log -Raw
    if($text -match "game sound unmuted" -and $text -notmatch "game sound in the Windows mixer put back"){
        Write-Warning "$scene did not finish recording: Amnesia_coop stays unmuted in the Windows volume mixer, mute it there by hand"
    }
    & (Join-Path $PSScriptRoot "coop_dev.ps1") -Kill | Out-Null
    & (Join-Path $PSScriptRoot "stills_sheet.ps1") -Video $video
}
