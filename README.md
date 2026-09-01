# FormatStudio

An Android JNI project originally created for decrypting Kugou `.kgm` encrypted format, now extended into a multi-functional tool for audio/video information parsing and video playback.

[中文](README-CN.md)

## Features

* Decrypt `.kgm` / `.kgm.flac` files → MP3

* Audio/video media information parsing (container, codec, resolution, bitrate, duration, etc.)

* Video playback (based on FFmpeg + ANativeWindow direct rendering)

* Dark/light theme adaptation

* Multi-Activity navigation (main screen, function selection)

* Support English

## Requirements

* Android Studio Hedgehog / Iguana or later

* Android SDK 26+ (minSdk 26)

* C++23 standard (CMake build)

* Only `arm64-v8a` architecture is supported (32-bit is not supported)

## Build Instructions

1. `git clone` this repository
2. Open the project root directory with Android Studio
3. Wait for Gradle Sync to complete
4. Click Build → Make Project

> **Note**: This project requires pre-built FFmpeg static libraries. Make sure the required `.a` files are present under `app/src/main/cpp/libs/` (or your CMake-configured path).

## Project Structure

```
app/src/main/
├── cpp/                          # C++ native code
│   ├── 3rdparty/avcpp/           # avcpp (FFmpeg C++ wrapper)
│   ├── YsPlayer/                 # YsPlayer player source
│   ├── include/                  # FFmpeg / OpenSSL / third-party headers
│   ├── libs/                     # Pre-built third-party static libraries (.a)
│   ├── FFmpeg.cpp / .h           # FFmpeg wrapper layer
│   ├── functions.cpp / .h        # JNI bridge functions
│   ├── native-lib.cpp            # C++ main entry
│   └── strings.cpp / .h          # C++ language library.
├── java/com/kgmdecoder/app/      # Java source
│   ├── MainActivity.java         # Main screen (console + video playback)
│   ├── Selecting.java            # Feature selection page
│   └── Help.java                 # Help page
├── jniLibs/arm64-v8a/            # Pre-built .so libraries
├── res/                          # Resources (layouts, colors, icons)
└── AndroidManifest.xml           # App manifest
```

## Tech Stack

| Component        | Technology                                              |
| ---------------- | --------------------------------------------------------|
| Language         | Java + C++ (JNI)                                        |
| Media Processing | FFmpeg 5.x / 6.x / 7.x (GPL), include lame, x264, x265  |
| C++ Wrapper      | avcpp (BSD-3)                                           |
| Video Player     | YsPlayer (Apache-2.0)                                   |
| Rendering        | ANativeWindow + OpenGL ES                               |
| Build            | Gradle + CMake                                          |
| UI               | Native Android View + ConstraintLayout                  |

## Signing

* This repository does not provide signing keys

* Please create your own keystore when building

## Third-Party Libraries & Licenses

This project uses the following third-party libraries. See [THIRD\_PARTY\_NOTICES.md](THIRD_PARTY_NOTICES.md) for full license information.

| Library           | License                                             | Notes                                                                   |
| ----------------- | --------------------------------------------------- | ----------------------------------------------------------------------- |
| FFmpeg            | GPL-2.0-or-later / GPL-3.0-or-later (combined work) | Statically linked (`.a`) with GPL components enabled (libx264, libx265) |
| avcpp             | BSD-3-Clause                                        | FFmpeg C++ wrapper                                                      |
| YsPlayer          | Apache-2.0                                          | Video player                                                            |
| OpenSSL           | Apache-2.0                                          | Crypto support                                                          |
| kugou-kgm-decoder | Anti 996 License v1.0                               | Generates `libkgm_decoder.so` for `.kgm` decryption                     |

## License

Because this project links FFmpeg **statically** (`.a`) with `--enable-gpl`, `--enable-libx264`, `--enable-libx265`, the resulting combined work must be distributed under the GNU General Public License. **This project source code is licensed under GPL-3.0-or-later**. See [LICENSE](LICENSE).

> Note: Individual third-party libraries retain their original respective licenses and must be complied with separately.
> Apache-2.0 components are compatible with GPL-3.0. The `kugou-kgm-decoder` module remains governed by Anti 996 License Version 1.0 and its terms are not overridden by the GPL-3.0-or-later of the overall project.

