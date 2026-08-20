# FormatStudio

An Android JNI project, originally for decoding the domestic encrypted `.kgm` format, now extended to support audio/video media info parsing and video playback.

[中文](README-CN.md)

## Features

- Decrypt `.kgm` / `.kgm.flac` files to MP3
- Audio/video media info parsing (container, codec, resolution, bitrate, duration, etc.)
- Video playback (FFmpeg + ANativeWindow direct rendering)
- Dark/Light theme adaptation
- Multi-Activity navigation (main screen, function selector, help page)

## Requirements

- Android Studio Hedgehog / Iguana or later
- Android SDK 24+ (minSdk 24)
- C++17 standard (CMake)
- Only `arm64-v8a` ABI (32-bit not yet supported)

## Build Steps

1. `git clone` this repository
2. Open the project root in Android Studio
3. Wait for Gradle sync
4. Click Build → Make Project

> **Note**: This project requires pre-built FFmpeg shared libraries. Ensure the required `.so` files are placed under `app/src/main/jniLibs/arm64-v8a/`.

## Project Structure

```
app/src/main/
├── cpp/                          # C++ native code
│   ├── 3rdparty/avcpp/           # avcpp (FFmpeg C++ wrapper)
│   ├── YsPlayer/                 # YsPlayer source
│   ├── include/                  # FFmpeg / OpenSSL / third-party headers
│   ├── libs/                     # Pre-built third-party libs
│   ├── FFmpeg.cpp / .h           # FFmpeg wrapper layer
│   ├── functions.cpp / .h        # JNI bridge functions
│   └── native-lib.cpp            # C++ main entry
├── java/com/kgmdecoder/app/      # Java source
│   ├── MainActivity.java         # Main screen (console + video)
│   ├── Selecting.java            # Function selector page
│   └── Help.java                 # Help page
├── jniLibs/arm64-v8a/            # Pre-built .so libraries
├── res/                          # Resources (layout, colors, icons)
└── AndroidManifest.xml           # App manifest
```

## Tech Stack

| Component | Technology |
|-----------|------------|
| Language | Java + C++ (JNI) |
| Media processing | FFmpeg 5.x (LGPL) |
| C++ wrapper | avcpp (BSD-3) |
| Video playback | YsPlayer (Apache-2.0) |
| Rendering | ANativeWindow + OpenGL ES |
| Build | Gradle + CMake |
| UI | Native Android View + ConstraintLayout |

## Signing

- No signing key provided in this repository
- Create your own keystore when building

## Third-Party Libraries & Licenses

This project uses the following third-party libraries. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for full license information.

| Library | License | Notes |
|---------|---------|-------|
| FFmpeg | GPL-2.0+ | Dynamically linked, GPL components enabled |
| avcpp | BSD-3-Clause | FFmpeg C++ wrapper |
| YsPlayer | Apache-2.0 | Video player |
| OpenSSL | Apache-2.0 | Crypto support |
| kugou-kgm-decoder | Anti 996 License v1.0 | Generates `libkgm_decoder.so` for `.kgm` decryption |

## License

Because this project uses a build of FFmpeg compiled with `--enable-gpl`, `--enable-libx264`, and `--enable-libx265`, the combined work must be licensed under the GNU General Public License (GPL) version 2.0 or later. This project's source code is therefore licensed under **GPL-2.0-or-later**. See [LICENSE](LICENSE).

Third-party library licenses are independent and must also be complied with separately.
