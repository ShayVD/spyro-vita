# Third-party notices

Spyro Vita's own code (everything outside `third_party/`) is under the MIT licence in
[LICENSE](LICENSE). The VPK (`eboot.bin`) also contains code from the projects below, compiled in
or statically linked. Their licences apply to their parts. Full licence texts are in
[`licenses/`](licenses/), which the VPK carries too (`ux0:app/SPYV00001/licenses/`).

## The game itself is not included

*Spyro the Dragon* (1998) is © its respective owners. This repository and the VPK contain none of
its data: no executable, levels, models, textures, sounds or music. Players supply their own disc
and extract its files themselves (`tools/extract_disc.py`).

## LiveArea artwork (not under the MIT licence)

The bubble icon, the LiveArea background and the start gate (`assets/livearea/`, packed into the
VPK's `sce_sys/`) are not our work, and the MIT licence doesn't cover them:

- **Icon and gate:** the *Spyro the Dragon* logo. Spyro and Spyro the Dragon are trademarks of
  Activision Publishing, Inc.
- **Background:** concept art by Simon Kopp (2017/18) for *Spyro Reignited Trilogy*, Airborn
  Studios. It's shown whole, with the artist's signature and the studio marks.

No licence has been obtained for these images. They're included at the project owner's decision
and will be replaced on request from the rights holders. Without `assets/livearea/`, the build
draws original art instead (`tools/vita/make_livearea.py`, decisions D55 and D59).

## What is in the VPK

The list comes from the linker map of the Vita build (`-Wl,-Map`): every archive that adds at least
one object to `eboot.bin`. It was built with VitaSDK 2026.08 (built 2026-08-25, as in the
`vitasdk/vitasdk` Docker image CI uses).

| Component | Version | Licence | Text |
|---|---|---|---|
| [TheMobyCollective/spyro-1](https://github.com/TheMobyCollective/spyro-1) (the decompilation) | submodule `d2ac00b` | CC0 1.0 | [licenses/CC0-1.0.txt](licenses/CC0-1.0.txt) |
| [PsyCross](https://github.com/OpenDriver2/PsyCross) (PsyQ reimplementation) | submodule `e56e4cd` | MIT | below |
| [vitaGL](https://github.com/Rinnegatamante/vitaGL) | vdpm `0.0.0.r1488.g2bdbe89` | LGPL-3.0 (with parts under other licences, below) | [licenses/LGPL-3.0.txt](licenses/LGPL-3.0.txt), [licenses/GPL-3.0.txt](licenses/GPL-3.0.txt) |
| [vitaShaRK](https://github.com/Rinnegatamante/vitaShaRK) | vdpm `1.7` | LGPL-3.0; xxHash BSD-2-Clause | as vitaGL; xxHash below |
| [SceShaccCgExt](https://github.com/bythos14/SceShaccCgExt) | vdpm `1.0.1` | LGPL-2.1 | [licenses/LGPL-2.1.txt](licenses/LGPL-2.1.txt) |
| [math-neon](https://github.com/Rinnegatamante/math-neon) (`libmathneon`) | vdpm `0.0.0.r11.g0faab81` | MIT | below |
| [SDL2](https://www.libsdl.org) | vdpm `sdl2 2.32.8` | zlib; yuv2rgb BSD-3-Clause | below |
| [newlib](https://github.com/vitasdk/newlib) (`libc`, `libm`) | VitaSDK 2026.08 | BSD-style and others, per file | [licenses/newlib-COPYING.NEWLIB.txt](licenses/newlib-COPYING.NEWLIB.txt) |
| GCC runtime (`libgcc`, `libstdc++`) | GCC of VitaSDK 2026.08 | GPL-3.0 with the GCC Runtime Library Exception 3.1 | [licenses/GCC-RUNTIME-EXCEPTION-3.1.txt](licenses/GCC-RUNTIME-EXCEPTION-3.1.txt), [licenses/GPL-3.0.txt](licenses/GPL-3.0.txt) |
| [vita-headers](https://github.com/vitasdk/vita-headers) (the `Sce*_stub` import stubs) | VitaSDK 2026.08 | MIT | below |
| [taiHEN](https://github.com/yifanlu/taiHEN) (`taihen_stub`) | vdpm `0.11` | MIT | below |

`libpthread` (pthread-embedded, LGPL-2.1) is on the link line, but the map shows no object taken
from it.

Not in the VPK, and not distributed by this project: the tools the build runs (CMake, Ninja,
Python, VitaSDK's compiler and packers, the decomp's GCC 2.7.2 and maspsx for the matching build,
Docker images), and Sony's `libshacccg.suprx`, which players extract from their own Vita.

## The LGPL libraries (vitaGL, vitaShaRK, SceShaccCgExt)

These are linked statically. The LGPL allows that in a program under another licence, as long as
the user can rebuild the program against a modified version of the library. Everything needed for
that is this repository's source, at the commit the VPK was built from, and the build instructions
in [README.md](README.md#building-from-source): install the modified library into VitaSDK and
build again. The libraries' own source is at the links above (vitaGL at commit `2bdbe89`).

**Note on vitaGL's GLSL preprocessor.** vitaGL's `source/utils/preprocessor/` (linked in through its
GLSL translator) credits [john-blackburn/preprocessor](https://github.com/john-blackburn/preprocessor),
whose repository is under GPL-3.0. If that code is under GPL-3.0 in vitaGL too, a binary that
contains it must be distributed under GPL-3.0's terms as a whole: with the complete source of the
port. The port's code is MIT, which GPL-3.0 allows, so publishing the source of every released VPK
covers this case as well. This is upstream's to clarify. It is noted here so that releases keep
their source available.

## Notices

### PsyCross

```
MIT License

Copyright (c) 2020 REDRIVER2 Project

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

PsyCross's `src/gte/half_float.cpp` follows a public-domain gist by Fabian Giesen (rygorous).
`patches/PsyCross/` holds this port's changes to PsyCross, under the MIT licence like PsyCross.

### TheMobyCollective/spyro-1

The decompilation is dedicated to the public domain under CC0 1.0 Universal
([licenses/CC0-1.0.txt](licenses/CC0-1.0.txt)). The dedication covers the decompilation
project's own work. The original game it reconstructs remains © its owners (see above).
`patches/spyro-1/` and `patches/spyro-1-port/` hold this port's changes. They are under this
project's MIT licence.

### vitaGL and vitaShaRK

```
Copyright 2017, 2018, 2019, 2020 Rinnegatamante

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published
by the Free Software Foundation, version 3 of the License, or (at your
option) any later version.
```

vitaGL contains code under other licences, linked in with it:

- `source/utils/etc1_utils.c`: Copyright 2009 Google Inc., Apache License 2.0
  ([licenses/Apache-2.0.txt](licenses/Apache-2.0.txt)).
- `source/utils/atitc_utils.c`: Copyright (c) 2010-2012 cocos2d-x.org, Copyright (c) 2013-2014
  Chukong Technologies Inc., MIT licence (the same text as PsyCross's above, with these copyright
  lines).
- `source/utils/eac_utils.c`, `source/utils/etc_utils.c`:

  ```
  Copyright (c) 2015 Harm Hanemaaijer <fgenfb@yahoo.com>

  Permission to use, copy, modify, and/or distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
  ```

- `source/utils/stb_dxt.h`: stb_dxt by Sean Barrett, public domain.
- `source/utils/preprocessor/`: see the note above.

vitaShaRK contains xxHash:

```
xxHash - Extremely Fast Hash algorithm
Copyright (C) 2012-2023 Yann Collet

BSD 2-Clause License (https://www.opensource.org/licenses/bsd-license.php)

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

   * Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
   * Redistributions in binary form must reproduce the above
     copyright notice, this list of conditions and the following disclaimer
     in the documentation and/or other materials provided with the
     distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### SceShaccCgExt

By bythos14, under the GNU Lesser General Public License 2.1
([licenses/LGPL-2.1.txt](licenses/LGPL-2.1.txt)).

### math-neon

```
The MIT License (MIT)

Copyright (c) 2015 Lachlan Tychsen-Smith (lachlan.ts@gmail.com)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

### SDL2

```
Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```

SDL2's `src/video/yuv2rgb` (linked in):

```
Copyright (c) 2016, Adrien Descamps
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of yuv2rgb nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

SDL2's `SDL_malloc.c` is Doug Lea's malloc, public domain.

### newlib

newlib is a collection of code from many sources, each file under its own licence (mostly
BSD-style). Their notices are collected in
[licenses/newlib-COPYING.NEWLIB.txt](licenses/newlib-COPYING.NEWLIB.txt), newlib's own
`COPYING.NEWLIB`.

### GCC runtime libraries

`libgcc` and `libstdc++` are under GPL-3.0 with the GCC Runtime Library Exception 3.1, which
allows them in programs under any licence when compiled with GCC
([licenses/GCC-RUNTIME-EXCEPTION-3.1.txt](licenses/GCC-RUNTIME-EXCEPTION-3.1.txt)).

### vita-headers (import stubs)

```
Copyright 2017 vitasdk

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

### taiHEN (import stubs)

```
The MIT License (MIT)

Copyright (c) 2016 Yifan Lu

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Reference-only code

`../psxrecomp-ref` (PolyForm Noncommercial 1.0.0) was read for techniques during development and
nothing from it is in this repository. `tools/check_reference_overlap.py` compares every line of 40
or more characters of its own code with this repository's tracked files. Run on 2026-10-31, the
35 shared lines were CMake idioms, `glTexParameteri` calls, `sys.path` lines and the PS1 GTE
register lists that come from PsyCross's `MFC2` (`src/psx/INLINE_C.C`), none from psxrecomp.
OpenPete was not used at all.
