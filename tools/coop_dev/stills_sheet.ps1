<#
Puts the stills a recording saved (Dev RecordStills) on one contact sheet, to check a recording at a glance.

  .\stills_sheet.ps1 -Video ..\captures\02_storage_chase_host.mp4
#>
param([Parameter(Mandatory)][string]$Video, [int]$Columns = 4)

Add-Type -AssemblyName System.Drawing
$dir = [IO.Path]::ChangeExtension($Video, $null).TrimEnd('.') + "_frames"
$tiles = Get-ChildItem $dir -Filter *.bmp | Sort-Object Name
if($tiles.Count -eq 0){ "No stills in $dir"; return }

$W = 480; $H = 270
$rows = [math]::Ceiling($tiles.Count / $Columns)
$sheet = New-Object System.Drawing.Bitmap ($W * $Columns), ($H * $rows)
$g = [System.Drawing.Graphics]::FromImage($sheet)
$font = New-Object System.Drawing.Font 'Arial', 14
for($i = 0; $i -lt $tiles.Count; $i++){
    $img = [System.Drawing.Image]::FromFile($tiles[$i].FullName)
    $x = ($i % $Columns) * $W; $y = [math]::Floor($i / $Columns) * $H
    $g.DrawImage($img, $x, $y, $W, $H)
    $g.DrawString($tiles[$i].BaseName, $font, [System.Drawing.Brushes]::Yellow, $x + 4, $y + 4)
    $img.Dispose()
}
$out = [IO.Path]::ChangeExtension($Video, $null).TrimEnd('.') + "_sheet.jpg"
$sheet.Save($out, [System.Drawing.Imaging.ImageFormat]::Jpeg)
$g.Dispose(); $sheet.Dispose()
$out
