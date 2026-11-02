# README の画像と GIF を作り直す（docs/images）。先に -DPRI_BUILD_TOOLS=ON で構成して Release でビルドしておくこと。ffmpeg が必要
# 使い方: powershell -ExecutionPolicy Bypass -File Tools\MakeReadmeMedia.ps1 [-Ffmpeg <ffmpeg.exe のパス>]
param(
    [string] $Ffmpeg = "ffmpeg"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$images = Join-Path $root "docs\images"
$frames = Join-Path $root "build\readme-frames"
$tool = Join-Path $root "build\ReadmeMedia_artefacts\Release\ReadmeMedia.exe"

if (-not (Test-Path $tool)) { throw "ReadmeMedia.exe not found. Configure with -DPRI_BUILD_TOOLS=ON and build Release first." }

& $tool $images $frames
if ($LASTEXITCODE -ne 0) { throw "ReadmeMedia failed" }

# 色は全コマから 256 色を選び、ディザは模様が動かない bayer にする（変わらない部分を前のコマのまま残せるので軽くなる）
$gifs = @(
    @{ Name = "hero";      Scale = "" },
    @{ Name = "reduction"; Scale = "scale=960:-1:flags=lanczos," }
)

foreach ($gif in $gifs) {
    $filter = $gif.Scale + "split[a][b];[a]palettegen=max_colors=256:stats_mode=full[p];[b][p]paletteuse=dither=bayer:bayer_scale=3:diff_mode=rectangle"
    $out = Join-Path $images ($gif.Name + ".gif")
    & $Ffmpeg -y -loglevel error -framerate 20 -i (Join-Path $frames ($gif.Name + "\frame_%04d.png")) -filter_complex $filter -loop 0 $out
    if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed: " + $gif.Name }
    "{0}: {1:N0} KB" -f $out, ((Get-Item $out).Length / 1KB)
}

Remove-Item -Recurse -Force $frames
