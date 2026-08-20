# 第三方库许可证声明

本项目使用了以下开源第三方库。由于本项目使用了启用 GPL 组件的 FFmpeg 构建，整个项目按 GPL-2.0-or-later 分发。各第三方库各自的许可证仍需单独遵守。

[English](THIRD_PARTY_NOTICES.md)

---

## FFmpeg

- **许可证**: GPL-2.0-or-later
- **用途**: 音视频解码、编码、格式转换
- **仓库**: https://git.ffmpeg.org/ffmpeg.git
- **合规要求**:
  - 使用动态链接
  - 编译时启用了 `--enable-gpl`、`--enable-libx264`、`--enable-libx265`
  - 本项目作为整体按 GPL-2.0-or-later 分发
  - 需在应用内声明使用了 FFmpeg 及其 GPL 许可证

```
FFmpeg is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

FFmpeg is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
```

---

## avcpp

- **许可证**: BSD-3-Clause
- **用途**: FFmpeg C++ 封装层
- **仓库**: https://github.com/himpossible/avcpp
- **合规要求**:
  - 保留版权声明
  - 不得使用贡献者名称进行背书

```
Copyright (c) respective contributors

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice.
2. Redistributions in binary form must reproduce the above copyright notice.
3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.
```

---

## YsPlayer

- **许可证**: Apache-2.0
- **用途**: Android 视频播放器（基于 FFmpeg）
- **合规要求**:
  - 保留版权声明和许可证文本
  - 不得使用贡献者名称进行背书

```
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```

---

## OpenSSL

- **许可证**: Apache-2.0 (OpenSSL 3.0+)
- **用途**: 加密支持
- **仓库**: https://www.openssl.org/
- **合规要求**:
  - 保留版权声明和许可证文本
  - 在产品文档中声明使用了 OpenSSL

```
Copyright (c) 1998-2024 The OpenSSL Project Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.openssl.org/source/license.html
```

---

## kugou-kgm-decoder

- **许可证**: 反 996 许可证版本 1.0
- **用途**: 生成 `libkgm_decoder.so`，用于解密酷狗 `.kgm` 格式
- **仓库**: https://github.com/ghtz08/kugou-kgm-decoder
- **合规要求**:
  - 保留版权声明和许可证文本
  - 不得修改许可证内容
  - 请遵守所在司法管辖区劳动与就业相关法律法规

```
版权所有（c）<年份><版权持有人>

反996许可证版本1.0

在符合下列条件的情况下，特此免费向任何得到本授权作品的副本（包括源代码、文件和/或相关内容，以下
统称为“授权作品”）的个人和法人实体授权：被授权个人或法人实体有权以任何目的处置授权作品，包括但
不限于使用、复制，修改，衍生利用、散布，发布和再许可：

1. 个人或法人实体必须在许可作品的每个再散布或衍生副本上包含以上版权声明和本许可证，不得自行修改。
2. 个人或法人实体必须严格遵守与个人实际所在地或个人出生地或归化地、或法人实体注册地或经营地
（以较严格者为准）的司法管辖区所有适用的与劳动和就业相关法律、法规、规则和标准。
3. 个人或法人不得以任何方式诱导或强迫其全职或兼职员工或其独立承包人以口头或书面形式同意直接或
间接限制、削弱或放弃其所拥有的，受相关与劳动和就业有关的法律、法规、规则和标准保护的权利或补救
措施。

该授权作品是"按原样"提供，不做任何明示或暗示的保证。在任何情况下，版权持有人均不承担因本软件或
本软件的使用或其他交易而产生、引起或与之相关的任何索赔、损害或其他责任。
```

---

## 合规建议

1. **FFmpeg 源码分发**: 如果你以二进制形式（APK）分发应用，应提供获取 FFmpeg 源码的方式（链接或直接提供）。推荐做法是在 README 或 About 页面中给出 FFmpeg 源码下载链接。

2. **许可证文件**: 各第三方库的完整许可证文本应包含在项目中，或提供可访问的链接。

3. **声明页面**: 建议在应用内添加「开源声明」页面，列出所有使用的第三方库及其许可证。

4. **GPL 合规**: 由于本项目的 FFmpeg 构建包含 GPL 组件（`--enable-gpl`、`libx264`、`libx265`），作为整体必须按 GPL-2.0-or-later 分发。本项目源代码因此按 GPL-2.0-or-later 发布。
