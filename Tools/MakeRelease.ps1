# 配布用の zip を作る（build\release）。プラグインを Release でビルドし直してから、LICENSE などと一緒にまとめる
# 使い方: powershell -ExecutionPolicy Bypass -File Tools\MakeRelease.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build"
$repository = "https://github.com/pritezza/PRIResonanceSuppressor"

# バージョンは CMakeLists.txt の project(... VERSION x.y.z) から取る
$cmakeLists = Get-Content -Raw (Join-Path $root "CMakeLists.txt")
if ($cmakeLists -notmatch 'project\(PRIResonanceSuppressor VERSION (\d+\.\d+\.\d+)') { throw "VERSION not found in CMakeLists.txt" }
$version = $Matches[1]

# 配布用のビルドは LTO を ON にする
$cache = Join-Path $build "CMakeCache.txt"
if (-not (Test-Path $cache)) { throw "build\CMakeCache.txt not found. Configure first (see README)." }
if (-not (Select-String -Quiet -Path $cache -Pattern '^PRI_ENABLE_LTO:BOOL=ON$')) { throw "PRI_ENABLE_LTO is OFF. Configure with -DPRI_ENABLE_LTO=ON first." }

# コミットしていない変更があると、zip の中身とタグのソースが一致しなくなる
if (git -C $root status --porcelain) { Write-Warning "The working tree has uncommitted changes." }

cmake --build $build --config Release --target PRIResonanceSuppressor_VST3 -- /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

$name = "PRIResonanceSuppressor-$version-win64"
$stage = Join-Path $build "release\$name"
$zip = Join-Path $build "release\$name.zip"

if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
if (Test-Path $zip) { Remove-Item -Force $zip }
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item -Recurse (Join-Path $build "PRIResonanceSuppressor_artefacts\Release\VST3\PRI Resonance Suppressor.vst3") $stage
Copy-Item (Join-Path $root "LICENSE") (Join-Path $stage "LICENSE.txt")
Copy-Item (Join-Path $root "THIRD_PARTY_NOTICES.txt") $stage

# zip に入れる説明（メモ帳で文字化けしないよう BOM 付きの UTF-8、改行は CRLF）
$readme = @"
PRI Resonance Suppressor $version
Spectral resonance suppressor plug-in (VST3 for Windows)
$repository

[English]

Requirements: Windows 10 or 11 (64-bit), and a DAW that supports VST3.

Installation:
1. Copy the "PRI Resonance Suppressor.vst3" folder into
   C:\Program Files\Common Files\VST3\
   Windows asks for administrator permission.
2. Start your DAW, and rescan plug-ins if it doesn't find the new one.

Uninstalling:
- Delete the "PRI Resonance Suppressor.vst3" folder.
- Display settings are saved in %APPDATA%\PRITEZZA\PRIResonanceSuppressor.settings.
  Delete this file too if you want to remove everything.

Usage: see the README at $repository

License: GNU Affero General Public License v3 (LICENSE.txt). WITHOUT ANY WARRANTY.
Source code (tag v$version): $repository
Third-party licences: THIRD_PARTY_NOTICES.txt

[日本語]

必要なもの: Windows 10 / 11（64 ビット）、VST3 に対応した DAW

インストール:
1. 「PRI Resonance Suppressor.vst3」フォルダを
   C:\Program Files\Common Files\VST3\
   にコピーします。管理者の許可を求められます。
2. DAW を起動します。新しいプラグインが見つからないときは、プラグインを再スキャンします。

アンインストール:
- 「PRI Resonance Suppressor.vst3」フォルダを削除します。
- 表示の設定は %APPDATA%\PRITEZZA\PRIResonanceSuppressor.settings に保存されています。
  すべて消す場合は、このファイルも削除します。

使い方: $repository/blob/main/README.ja.md を参照してください。

ライセンス: GNU Affero General Public License v3（LICENSE.txt）。無保証です。
ソースコード（タグ v$version）: $repository
第三者のソフトウェアのライセンス: THIRD_PARTY_NOTICES.txt
"@
[IO.File]::WriteAllText((Join-Path $stage "README.txt"), ($readme -replace "`r?`n", "`r`n"), (New-Object Text.UTF8Encoding $true))

# zip の中のパスは / で区切る（Windows PowerShell の Compress-Archive は \ で区切ることがある）
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::Open($zip, [IO.Compression.ZipArchiveMode]::Create)

try {
    foreach ($file in Get-ChildItem -Recurse -File $stage) {
        $entry = $file.FullName.Substring($stage.Length + 1).Replace('\', '/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $entry, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
}
finally {
    $archive.Dispose()
}

"{0}: {1:N0} KB" -f $zip, ((Get-Item $zip).Length / 1KB)
