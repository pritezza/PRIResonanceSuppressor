# 開発用の情報

[English](DEVELOPMENT.md) | **日本語**

プラグインを確かめるツール、負荷を測るツール、README の画像を作り直すツール、配布用の zip を作るスクリプトについて書いています。プラグインをビルドするだけなら、README の [ソースからのビルド](../README.ja.md#ソースからのビルド) を参照してください。

## ツールのビルド

ツールは既定ではビルドされません。`PRI_BUILD_TOOLS=ON` で構成します。

```powershell
cmake -S . -B build -DPRI_BUILD_TOOLS=ON
cmake --build build --config Release
```

各ツールは `build/<ツール名>_artefacts/Release/<ツール名>.exe` にできます。

**ビルドの時間**:
- PluginHostTest 以外のツールは、プラグインのソースと JUCE をそれぞれコンパイルするので、ビルドの時間がかなり増えます。
- ツールを ON にしたままプラグインだけをビルドするときは、`cmake --build build --config Release --target PRIResonanceSuppressor_VST3` とします。
- ツールを作らないようにするには、`-DPRI_BUILD_TOOLS=OFF` で構成し直します。

**表示の設定**: エディターを開くツールは、表示の設定（テーマなど）を切り替えます。そのため、実行前に `%APPDATA%\PRITEZZA\PRIResonanceSuppressor.settings` を退避し、終わったら元に戻します。

## EditorSnapshot

[Tools/EditorSnapshot.cpp](../Tools/EditorSnapshot.cpp)。プラグインのソースからプロセッサーとエディターを作り、処理と表示を確かめます。

```powershell
build\EditorSnapshot_artefacts\Release\EditorSnapshot.exe <出力フォルダ>
```

結果を 1 行ずつ出します。合否の自動判定はないので、下の「目安」と比べてください。

| 確認 | 内容 | 目安 |
|---|---|---|
| 遅延の一致 | 削らない設定で、バイパスを切り替えながら処理する。出力と「入力を FFT の点数分遅らせた音」の差を見る。48 kHz の 3 段、96 / 192 kHz、伝えた長さより長いブロックで行う | 差の最大は −120 dB 前後（入力の RMS 比） |
| MIX | MIX 0% と 50% の出力が「元の音 +（処理後 − 元の音）× MIX」と一致するか | −200 dB |
| GR メーター | DEPTH 24 dB で強い共振を削り、点いた LED を数える | 10 / 10 |
| ステレオの扱い | 削らない設定で、どの扱いでも（途中の切り替えを含めて）音が変わらないか。R だけ・左右差だけに共振を足したときの削れ方 | L/R なら L、M/S なら中央がほぼそのまま残る |
| 遅延と状態の復元 | 停止中に RESOLUTION を変えたとき、報告する遅延が変わるか。状態を戻すと画面の大きさが戻り、Δ LISTEN が OFF になるか | 1024 → 4096、listen 0 |
| 帯域の欠けた音 | MP3 のように高域を切ったピンクノイズ（16 / 19.5 kHz）とローカット（80 Hz）を入れる。カットの手前に本物の共振も足して試す | カットだけなら 0 dB 前後。カットの手前の共振は削る |
| マウス操作 | 検出範囲の線のドラッグ、RESOLUTION のホイール・ドラッグ・クリック、UI SIZE | LOW と HIGH が互いを越えない |
| 画面 | 3870 Hz の共振を足したピンクノイズを処理しながら、エディターを PNG に書き出す: `rack.png`、`rack_bypass.png`、`rack_75.png`、`rack_settings.png`、`rack_analyzer.png`（ピークホールド、縦軸 60 dB）、`rack_oneline.png`（LINE GLOW 1 LINE）、`rack_black.png` / `rack_silver.png` / `rack_vintage.png` | 目で確認する |

## PluginHostTest

[Tools/PluginHostTest.cpp](../Tools/PluginHostTest.cpp)。ビルドした VST3 を、DAW と同じようにホストとして読み込みます。

```powershell
build\PluginHostTest_artefacts\Release\PluginHostTest.exe "build/PRIResonanceSuppressor_artefacts/Release/VST3/PRI Resonance Suppressor.vst3"
```

確かめること:
- **パラメータ**: 14 個の一覧と、オートメーションできるか。できないのは RESOLUTION、HI-RES FFT、STEREO の 3 つだけ。
- **遅延の一致**: バイパスをまたいで確かめる。48 kHz の 3 段と 96 kHz で行う。
- **遅延の変更**: 再生中に RESOLUTION を変えたとき、新しい遅延がホストに届くか。
- **状態**: 保存して別のインスタンスに読み込み、すべてのパラメータが一致するか。
- **エディター**: 開くか、大きさはいくつか。

## Benchmark

[Tools/Benchmark.cpp](../Tools/Benchmark.cpp)。負荷を測ります。

```powershell
build\Benchmark_artefacts\Release\Benchmark.exe
```

測るもの:
- **音の処理**: `processBlock` にかかる時間を、1 コアの何 % かで出す。FFT の点数、サンプルレート、ステレオの扱い、無音のそれぞれの場合で測る。
- **FFT**: `juce::dsp::FFT` の実数 FFT（順と逆の 1 組）と、比べるための半分の大きさの複素 FFT の時間。
- **細かい計算**: `decibelsToGain` と `log10` をビンの数だけ計算する時間。
- **画面**: 液晶 1 フレームの描画時間（100% と 150%、LINE GLOW の 3 LINES と 1 LINE）と、エディター全体の描画時間。

動画のエンコードなど重い処理がほかに動いていると、数字が大きく変わります。比べるときは同じ条件で続けて測ってください。

## README の画像

[Tools/MakeReadmeMedia.ps1](../Tools/MakeReadmeMedia.ps1) で、`docs/images` の画像と GIF を作り直します。このスクリプトは、まず `ReadmeMedia.exe`（[Tools/ReadmeMedia.cpp](../Tools/ReadmeMedia.cpp)）で画像と GIF のコマを書き出します。そのあと ffmpeg でコマを GIF にまとめます。

```powershell
powershell -ExecutionPolicy Bypass -File Tools\MakeReadmeMedia.ps1 -Ffmpeg <ffmpeg.exe のパス>
```

- デモの音はツールの中で合成しているので、素材の権利の心配はありません。
  - 冒頭の GIF とテーマの画像: ドラム、ベース、和音に共振を 3 つ足した音。
  - 削っている様子の GIF: 往復する共振を足したピンクノイズ。
- ffmpeg に PATH が通っていれば、`-Ffmpeg` は省けます。
- コマは `build/readme-frames` に書き出し、GIF ができたら消します。

## リリースの作成

[Tools/MakeRelease.ps1](../Tools/MakeRelease.ps1) で、プラグインをビルドし、配布用の zip を `build/release/PRIResonanceSuppressor-<バージョン>-win64.zip` に作ります。開発用のツールは要りません。

```powershell
powershell -ExecutionPolicy Bypass -File Tools\MakeRelease.ps1
```

- バージョンは CMakeLists.txt の `project(... VERSION ...)` から取ります。
- zip には、`.vst3` フォルダ、LICENSE.txt、THIRD_PARTY_NOTICES.txt、短い README.txt（インストール手順とソースの場所。英語と日本語）が入ります。
- LTO が OFF なら止まります。コミットしていない変更があれば警告します。
- そのあと、コミットに `v<バージョン>` のタグを付けて push し、GitHub のリリースに zip を添付します。
