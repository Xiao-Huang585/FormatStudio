# FormatStudio

一个 Android JNI 项目，最初用于解密酷狗 `.kgm` 加密格式，现已扩展为支持音视频信息解析、视频播放的多功能工具。

[English](README.md)

## 功能

- `.kgm` / `.kgm.flac` 文件解密 → MP3
- 音视频媒体信息解析（容器、编码器、分辨率、码率、时长等）
- 视频播放（基于 FFmpeg + ANativeWindow 直接渲染）
- 深色/浅色主题自适应
- 多 Activity 页面导航（主界面、功能选择、帮助页）

## 系统要求

- Android Studio Hedgehog / Iguana 或更高版本
- Android SDK 24+（minSdk 24）
- C++17 标准（CMake 编译）
- 仅支持 `arm64-v8a` 架构（暂不兼容 32 位）

## 构建步骤

1. `git clone` 本仓库
2. 用 Android Studio 打开项目根目录
3. 等待 Gradle Sync 完成
4. 点击 Build → Make Project

> **注意**：本项目需要预编译的 FFmpeg 动态库。请确保 `app/src/main/jniLibs/arm64-v8a/` 下包含所需的 `.so` 文件。

## 项目结构

```
app/src/main/
├── cpp/                          # C++ 原生代码
│   ├── 3rdparty/avcpp/           # avcpp（FFmpeg C++ 封装）
│   ├── YsPlayer/                 # YsPlayer 播放器源码
│   ├── include/                  # FFmpeg / OpenSSL / 第三方库头文件
│   ├── libs/                     # 预编译第三方库
│   ├── FFmpeg.cpp / .h           # FFmpeg 封装层
│   ├── functions.cpp / .h        # JNI 桥接函数
│   └── native-lib.cpp            # C++ 主入口
├── java/com/kgmdecoder/app/      # Java 源码
│   ├── MainActivity.java         # 主界面（控制台 + 视频播放）
│   ├── Selecting.java            # 功能选择页
│   └── Help.java                 # 帮助页
├── jniLibs/arm64-v8a/            # 预编译 .so 库
├── res/                          # 资源文件（布局、颜色、图标）
└── AndroidManifest.xml           # 应用清单
```

## 技术栈

| 组件 | 技术 |
|------|------|
| 语言 | Java + C++ (JNI) |
| 媒体处理 | FFmpeg 5.x (LGPL) |
| C++ 封装 | avcpp (BSD-3) |
| 视频播放 | YsPlayer (Apache-2.0) |
| 渲染 | ANativeWindow + OpenGL ES |
| 构建 | Gradle + CMake |
| UI | 原生 Android View + ConstraintLayout |

## 签名

- 本仓库不提供签名密钥
- 构建时请自行创建 keystore

## 第三方库及许可证

本项目使用了以下第三方库，请查阅 [THIRD_PARTY_NOTICES-CN.md](THIRD_PARTY_NOTICES-CN.md) 了解完整的许可证信息。

| 库 | 许可证 | 说明 |
|----|--------|------|
| FFmpeg | GPL-2.0+ | 动态链接，启用了 GPL 组件 |
| avcpp | BSD-3-Clause | FFmpeg C++ 封装 |
| YsPlayer | Apache-2.0 | 视频播放器 |
| OpenSSL | Apache-2.0 | 加密支持 |
| kugou-kgm-decoder | 反 996 License v1.0 | 生成 `libkgm_decoder.so` 用于解密 `.kgm` |

## 许可证

由于本项目使用了启用 `--enable-gpl`、`--enable-libx264` 和 `--enable-libx265` 构建的 FFmpeg，整体作品必须按 GNU 通用公共许可证（GPL）第 2.0 版或更高版本分发。本项目源代码因此按 **GPL-2.0-or-later** 发布。详见 [LICENSE](LICENSE)。

第三方库各自的许可证仍需单独遵守。
