# Third-Party License Notices

This project uses the following open-source third-party libraries. Because this project uses a build of FFmpeg with GPL components enabled, the combined work is distributed under GPL-2.0-or-later. Each third-party library's own license must also be complied with separately.

[ÖÐÎÄ](THIRD_PARTY_NOTICES-CN.md)

---

## FFmpeg

- **License**: GPL-2.0-or-later
- **Purpose**: Audio/video decoding, encoding, format conversion
- **Repository**: https://git.ffmpeg.org/ffmpeg.git
- **Compliance**:
  - Uses dynamic linking
  - Compiled with `--enable-gpl`, `--enable-libx264`, `--enable-libx265`
  - This project is also open-sourced under GPL-2.0-or-later as required
  - Must declare FFmpeg usage and GPL license in the app

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

- **License**: BSD-3-Clause
- **Purpose**: FFmpeg C++ wrapper layer
- **Repository**: https://github.com/himpossible/avcpp
- **Compliance**:
  - Retain copyright notice
  - Do not use contributor names for endorsement

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

- **License**: Apache-2.0
- **Purpose**: Android video player (based on FFmpeg)
- **Compliance**:
  - Retain copyright notice and license text
  - Do not use contributor names for endorsement

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

- **License**: Apache-2.0 (OpenSSL 3.0+)
- **Purpose**: Cryptography support
- **Repository**: https://www.openssl.org/
- **Compliance**:
  - Retain copyright notice and license text
  - Declare OpenSSL usage in product documentation

```
Copyright (c) 1998-2024 The OpenSSL Project Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.openssl.org/source/license.html
```

---

## kugou-kgm-decoder

- **License**: Anti 996 License Version 1.0
- **Purpose**: Generates `libkgm_decoder.so`, used for decrypting Kugou `.kgm` files
- **Repository**: https://github.com/ghtz08/kugou-kgm-decoder
- **Compliance**:
  - Retain copyright notice and license text
  - Do not modify the license text
  - Comply with labor and employment laws in your jurisdiction

```
Copyright (c) <year> <copyright holder>

Anti 996 License Version 1.0

Permission is hereby granted to any individual or legal entity obtaining a copy
of this licensed work (including the source code, documentation and/or related
items, hereinafter collectively referred to as the "licensed work"), free of
charge, to deal with the licensed work for any purpose, including without
limitation, the rights to use, reproduce, modify, prepare derivative works of,
publish, distribute and sublicense the licensed work, subject to the following
conditions:

1. The individual or the legal entity must conspicuously display, without
   modification, this License and the notice on each redistributed or
   derivative copy of the Licensed Work.
2. The individual or the legal entity must strictly comply with all applicable
   laws, regulations, rules and standards of the jurisdiction relating to labor
   and employment.
3. The individual or the legal entity shall not induce or force its employee(s),
   whether full-time or part-time, or its independent contractor(s), in any
   methods, to agree in oral or written form, to directly or indirectly restrict,
   weaken or give up any of their rights or remedies under such laws,
   regulations, rules and standards relating to labor and employment.

THE LICENSED WORK IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
COPYRIGHT HOLDER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE LICENSED WORK OR THE USE OR OTHER DEALINGS IN THE LICENSED
WORK.
```

---

## Compliance Recommendations

1. **FFmpeg Source Distribution**: If you distribute the app in binary form (APK), you should provide a way to obtain FFmpeg source code (link or direct download). The recommended approach is to include a FFmpeg source download link in the README or an About page.

2. **License Files**: The full license text of each third-party library should be included in the project or provided as an accessible link.

3. **Notices Page**: It is recommended to add an "Open Source Notices" page in the app listing all third-party libraries and their licenses.

4. **GPL Compliance**: Because this build of FFmpeg includes GPL components (`--enable-gpl`, `libx264`, `libx265`), the combined work must be distributed under GPL-2.0-or-later. The source code of this project is therefore released under GPL-2.0-or-later.
