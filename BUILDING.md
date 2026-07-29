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
