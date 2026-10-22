# Third-party software

The port is built with the following software, with thanks to its authors. No binary is distributed from this
repository: the `.nro` is built by each player from their own copy of the game, with these components in it.

## In the `.nro`

| Component | Used for | License | Source / license text |
|---|---|---|---|
| [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp) by TGP482 | The Xbox runtime the game runs on (kernel, Direct3D 8, DirectSound, XAPI), the CPU renderer, and the recompiler that makes the game's C++. In `sdk/`, with the port's changes ([TECHNICAL.md](docs/TECHNICAL.md#changes-to-the-sdk)) | MIT | [`sdk/LICENSE`](sdk/LICENSE) |
| [libnx](https://github.com/switchbrew/libnx) | Nintendo Switch homebrew library, and the font of the start screen | ISC | [libnx's LICENSE.md](https://github.com/switchbrew/libnx/blob/master/LICENSE.md) |
| [Mesa](https://mesa3d.org) 20.1, devkitPro's Switch port (`switch-mesa`) | OpenGL 4.3 and EGL for the GPU renderer, through its nouveau driver | MIT (most of it; see its license page) | [Mesa's license](https://docs.mesa3d.org/license.html), [devkitPro's mesa fork](https://github.com/devkitPro/mesa) |
| libdrm_nouveau, devkitPro's Switch port (`switch-libdrm_nouveau`) | The nouveau driver's access to the Switch's GPU | MIT | [devkitPro's libdrm_nouveau](https://github.com/devkitPro/libdrm_nouveau) |
| [newlib](https://sourceware.org/newlib/) (devkitA64) | C library | BSD-style, several authors | [newlib's COPYING.NEWLIB](https://sourceware.org/git/?p=newlib-cygwin.git;a=blob;f=COPYING.NEWLIB) |
| GCC runtime (libstdc++, libgcc; devkitA64) | C++ runtime | GPL-3.0 with the GCC Runtime Library Exception | [the exception](https://www.gnu.org/licenses/gcc-exception-3.1.html) |

libnx, Mesa, libdrm_nouveau, newlib and the GCC runtime come from [devkitPro](https://devkitpro.org)'s packages
and are not in this repository. The LZ4 decoder in `sdk/runtime/kernel/image.cpp` is the port's own code, written from the format's
description ([LZ4 block format](https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md) by Yann Collet).

## In the PC tools only

Built on your computer by `build.sh` to make the `.nro`; not part of it. CMake downloads them at the first build.

| Component | Used for | License |
|---|---|---|
| [Zydis](https://github.com/zyantific/zydis) v4.1.1 | Decoding the game's x86 code in `xbe_recomp` and `xbe_symbols` | MIT |
| [toml++](https://github.com/marzer/tomlplusplus) v3.4.0 | Reading `config/game.toml` | MIT |

## Used as references, not included

| Project | For |
|---|---|
| [XbSymbolDatabase](https://github.com/Cxbx-Reloaded/XbSymbolDatabase), [Cxbx-Reloaded](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded), [xemu](https://github.com/xemu-project/xemu) | Finding the XDK's functions in `default.xbe`; how the Xbox and its GPU behave |
| [extract-xiso](https://github.com/XboxDev/extract-xiso) | Making XISOs to test the disc reader with |
| [Repackinator / XboxToolkit](https://github.com/Team-Resurgent/Repackinator) by Team Resurgent | The CCI format |
| [ciso](https://github.com/antangelo/ciso) by antangelo | The CSO format, and CSO files to test with |

The SDK's Windows PC build also uses Dear ImGui and SDL ([`sdk/THIRD_PARTY.txt`](sdk/THIRD_PARTY.txt)); the
Switch port doesn't.
