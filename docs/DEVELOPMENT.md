# Development

**English** | [日本語](DEVELOPMENT.ja.md)

This page covers the tools for checking and measuring the plug-in, for regenerating the README images, and for packaging a release. If you only want to build the plug-in, see [Building from source](../README.md#building-from-source) in the README.

## Building the tools

The tools are not built by default. Configure with `PRI_BUILD_TOOLS=ON`:

```powershell
cmake -S . -B build -DPRI_BUILD_TOOLS=ON
cmake --build build --config Release
```

Each tool is written to `build/<Tool>_artefacts/Release/<Tool>.exe`.

**Build time:**
- Every tool except PluginHostTest compiles the plug-in sources and JUCE itself, so the tools add a lot of build time.
- To build only the plug-in while the tools are on, use `cmake --build build --config Release --target PRIResonanceSuppressor_VST3`.
- To stop building the tools, configure again with `-DPRI_BUILD_TOOLS=OFF`.

**Display settings:** The tools that open the editor change the display settings (theme and so on). They back up `%APPDATA%\PRITEZZA\PRIResonanceSuppressor.settings` before running and restore it when they finish.

## EditorSnapshot

[Tools/EditorSnapshot.cpp](../Tools/EditorSnapshot.cpp). It builds the processor and the editor from the plug-in sources, then checks the processing and the display.

```powershell
build\EditorSnapshot_artefacts\Release\EditorSnapshot.exe <output folder>
```

It prints one line per result. There is no automatic pass or fail; compare the numbers with the "Expected" column.

| Check | What it does | Expected |
|---|---|---|
| Latency alignment | Processes with settings that cut nothing, while switching BYPASS on and off. It then compares the output with the input delayed by the FFT size. Runs at 48 kHz for each RESOLUTION, at 96 and 192 kHz, and with blocks longer than announced. | Max error around −120 dB (relative to RMS) |
| MIX | Checks that the output at MIX 0% and 50% equals dry + (wet − dry) × MIX. | −200 dB |
| GR meter | Cuts a strong resonance with DEPTH 24 dB and counts the lit LEDs. | 10 / 10 |
| Stereo modes | Checks that nothing changes when nothing is cut, in every mode and while switching. It then adds a resonance only in R, or only in the side signal. | L/R leaves L almost untouched; M/S leaves mid almost untouched |
| Latency and state | Checks that RESOLUTION changes the reported latency while stopped. Checks that restoring a state brings back the UI size and turns Δ LISTEN off. | 1024 → 4096; listen 0 |
| Band-limited material | Feeds pink noise cut like an MP3 (16 / 19.5 kHz) or with a low cut (80 Hz), then real resonances just below the cutoff. | Near 0 dB on the plain cutoffs. Resonances below the cutoff are still cut. |
| Mouse | Drags the detection-range lines, operates RESOLUTION (wheel, drag, click), and selects UI SIZE. | LOW and HIGH never cross |
| Snapshots | Writes PNG files of the editor while processing pink noise with a resonance at 3870 Hz: `rack.png`, `rack_bypass.png`, `rack_75.png`, `rack_settings.png`, `rack_analyzer.png` (peak hold, 60 dB range), `rack_oneline.png` (LINE GLOW 1 LINE), and `rack_black.png` / `rack_silver.png` / `rack_vintage.png`. | Check them by eye |

## PluginHostTest

[Tools/PluginHostTest.cpp](../Tools/PluginHostTest.cpp). It loads the built VST3 the way a DAW does.

```powershell
build\PluginHostTest_artefacts\Release\PluginHostTest.exe "build/PRIResonanceSuppressor_artefacts/Release/VST3/PRI Resonance Suppressor.vst3"
```

It checks:
- **Parameters:** lists all 14 parameters and whether each can be automated. Only RESOLUTION, HI-RES FFT and STEREO can't.
- **Latency alignment across bypass:** at 48 kHz for each RESOLUTION, and at 96 kHz.
- **Latency change:** whether a RESOLUTION change during playback reaches the host as a new latency.
- **State:** saves the state, loads it into another instance, and checks that every parameter matches.
- **Editor:** whether the editor opens, and its size.

## Benchmark

[Tools/Benchmark.cpp](../Tools/Benchmark.cpp). It measures the CPU load.

```powershell
build\Benchmark_artefacts\Release\Benchmark.exe
```

It measures:
- **Audio processing:** the time `processBlock` takes, as a percentage of one core. Measured for several configurations: FFT size, sample rate, stereo mode and silence.
- **FFT:** one forward and inverse real FFT from `juce::dsp::FFT`, and a half-size complex FFT for comparison.
- **Small operations:** `decibelsToGain` and `log10` computed for every bin.
- **Display:** drawing time of one analyzer frame at 100% and 150%, with LINE GLOW at 3 LINES and at 1 LINE, plus the whole editor.

Other heavy processes, such as video encoding, change the results a lot. When comparing, measure in the same conditions, one run right after another.

## README images

[Tools/MakeReadmeMedia.ps1](../Tools/MakeReadmeMedia.ps1) regenerates the images and GIFs in `docs/images`. It runs `ReadmeMedia.exe` ([Tools/ReadmeMedia.cpp](../Tools/ReadmeMedia.cpp)), which writes the images and the GIF frames. It then uses ffmpeg to turn the frames into GIFs.

```powershell
powershell -ExecutionPolicy Bypass -File Tools\MakeReadmeMedia.ps1 -Ffmpeg <path to ffmpeg.exe>
```

- The demo sounds are synthesised inside the tool, so there are no rights issues.
  - For the main GIF and the theme images: drums, bass and chords with three resonances added.
  - For the reduction GIF: pink noise with a sweeping resonance.
- Leave out `-Ffmpeg` if ffmpeg is on your PATH.
- The frames are written to `build/readme-frames` and deleted when the GIFs are done.

## Making a release

[Tools/MakeRelease.ps1](../Tools/MakeRelease.ps1) builds the plug-in and writes the release zip to `build/release/PRIResonanceSuppressor-<version>-win64.zip`. It doesn't need the development tools.

```powershell
powershell -ExecutionPolicy Bypass -File Tools\MakeRelease.ps1
```

- The version comes from `project(... VERSION ...)` in CMakeLists.txt.
- The zip contains the `.vst3` folder, LICENSE.txt, THIRD_PARTY_NOTICES.txt and a short README.txt (installation and source location, in English and Japanese).
- The script stops if LTO is off, and warns if there are uncommitted changes.
- Then tag the commit as `v<version>`, push the tag, and attach the zip to a GitHub release.
