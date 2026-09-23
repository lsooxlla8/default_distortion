# Building default_distortion

The project uses CMake 3.22 or newer, C++20, and JUCE 8.0.15. JUCE is fetched
automatically during the first configure.

## macOS

Requirements: Xcode command-line tools and CMake.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The build produces AU, VST3, and Standalone formats. The minimum deployment
target is macOS 11. A universal Intel/Apple Silicon build can be configured
with:

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
```

## Windows

Requirements: 64-bit Windows, Visual Studio 2022 with the Desktop
development with C++ workload, and CMake.

Run from a Developer PowerShell:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The build produces VST3 and Standalone formats.

## Linux

The Linux configuration produces VST3, LV2, and Standalone formats. On Ubuntu
24.04, install the JUCE dependencies with:

```sh
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake \
  libasound2-dev libjack-jackd2-dev \
  libfontconfig1-dev libfreetype-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
  libxinerama-dev libxrandr-dev libxrender-dev libxi-dev \
  libglu1-mesa-dev mesa-common-dev libegl-dev
```

Then build and test:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

WebKit and Curl are deliberately disabled because the plug-in does not use a
web browser or networking.

## Auto Gain reference tables

Ordinary Auto Gain is entirely predictive: it reads the generated lookup
tables from the current smoothed DSP parameters and linearly reaches the new
coefficient within the same audio block. It never measures or follows the
programme RMS; that behaviour belongs exclusively to Smart Auto Gain.
The deterministic reference signal is a -12 dBFS peak sine, so regeneration
is repeatable and independent of whatever audio happens to be playing.
**Every change to a distortion algorithm, its
Drive/Character/Asymmetry mapping, stage behaviour, DC filtering, or reference
calibration must regenerate and commit all four tables.**

Build the test generator first, then regenerate the main table, the dense
Spectral Clip table, the Sine Erosion table with its additional Secondary axis,
and the focused table for algorithm-specific secondary controls:

```sh
cmake --build build --config Release --target DefaultDistortionTests --parallel
./build/DefaultDistortionTests_artefacts/Release/DefaultDistortionTests \
  --dump-auto-gain-table > Source/AutoGainTable.h
./build/DefaultDistortionTests_artefacts/Release/DefaultDistortionTests \
  --dump-spectral-auto-gain-table > Source/SpectralAutoGainTable.h
./build/DefaultDistortionTests_artefacts/Release/DefaultDistortionTests \
  --dump-sine-erosion-auto-gain-table > Source/SineErosionAutoGainTable.h
./build/DefaultDistortionTests_artefacts/Release/DefaultDistortionTests \
  --dump-secondary-auto-gain-table > Source/SecondaryAutoGainTable.h
cmake --build build --config Release --target DefaultDistortionTests --parallel
ctest --test-dir build -C Release --output-on-failure
```

`Tests/DspTests.cpp` is the authoritative generator. The main table covers all
30 internal mode IDs at four sample rates, all eight stage counts, and a grid
across Drive, Character, and Asymmetry. Spectral Clip has a denser dedicated
table because its makeup curve changes more sharply between the main grid
points. Sine Erosion has a dedicated table so Secondary can be calibrated without
needlessly multiplying every other mode's table by that extra dimension.
The secondary-control table covers Tape Bias, Transformer Air Gap, Downsample
Jitter, Bit Crusher Dither, and Schmitt Slew without adding a redundant axis
to the other 25 modes.

## GitHub Actions and releases

`.github/workflows/build-and-release.yml` builds and tests:

- universal macOS AU, VST3, and Standalone;
- Windows x64 VST3 and Standalone;
- Linux x64 VST3, LV2, and Standalone.

Pushes and pull requests build downloadable CI artifacts. Pushing a tag whose
name begins with `v`, for example `v0.5.7`, also creates a GitHub Release and
attaches the three platform packages. The numeric tag should match the version
declared by `project(... VERSION ...)` in `CMakeLists.txt`.

macOS CI artifacts are ad-hoc signed. A broadly distributed macOS release
still requires Developer ID signing and Apple notarization. Windows binaries
are unsigned until a code-signing certificate is added to the release
workflow.
