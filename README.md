# Spider-Man 2 (Xbox, 2004) — static recompilation, Nintendo Switch target

An unofficial port of the **original Xbox** version of *Spider-Man 2* (Treyarch, 2004) to Nintendo Switch homebrew,
by **SantiBros**. The game's x86 code is statically recompiled to C++ ahead of time; the Xbox kernel, Direct3D 8,
DirectSound and XAPI are reimplemented on the host. No emulator.

Built on [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp) by TGP482 (MIT), kept in `sdk/`.

> [!IMPORTANT]
> This repository contains **no game files and no code generated from the game**. You need your own copy of the
> game. Everything derived from it (the recompiled code, the build) is produced locally from your copy.

## Supported release

| | |
|---|---|
| Title | Spider-Man 2, title ID `4156002B` (AV-043), version 4, all regions flag |
| `default.xbe` SHA-1 | `853bcb253c5baea2d7e2d9b59e5888b4c87bb402` |
| XDK | 5849 (XAPILIB, D3D8, D3DX8, XGRAPHC, XMV, DSOUND, LIBCMT, LIBCPMT) |

## Status

- [x] XBE analysed: 120 kernel imports, XDK 5849 libraries, ~16% of game instructions are SSE, x87 used lightly
- [x] Whole game recompiles: 26,872 functions, 1.37M instructions (`xbe_recomp`, ~4 s)
- [x] Generated code compiles for ARM64 (GCC 13 `-mcpu=cortex-a57`, all 129 files, 28 MB of objects)
- [x] Boots headless on Linux (x86-64): kernel + XAPI init, title/save images, Direct3D device, DirectSound with its
      effects image, first push-buffer draws, then file loading from `D:\data\...`
- [x] Runs with the disc's data files (headless, 4 minutes, no crash): loads `game.ini`, `AMALGA_XB.PAK` and every
      sound bank, plays the Activision and Treyarch logo movies, sits on the title screen, then loops the attract
      movies as the console does when Start is not pressed
- [ ] **Next: vertex programs by address.** The game loads raw vertex programs into the GPU's program memory
      (`D3DDevice_LoadVertexShaderProgram`) and picks them with `D3DDevice_SelectVertexShader`; the SDK only does
      `CreateVertexShader` + `SetVertexShader`, so every draw is dropped (0 draws per frame so far)
- [ ] Frame pacing in the headless harness (it presents thousands of frames a second; keep `REF` off or large)
- [ ] Switch platform layer (libnx): guest memory window, threads, files, timers, audio, HID
- [ ] Switch GPU backend (OpenGL/EGL or deko3d) + GLSL output from the shader translator
- [ ] PC-side builder that turns the user's `default.xbe` into the `.nro`

## Layout

```
assets/        your default.xbe and the disc's files (ignored by git)
config/        game.toml: recompiler input (addresses and names only)
generated/     recompiled code from your copy (ignored by git)
src/common/    game info for the runtime
src/game/      game hooks (host replacements of recompiled functions)
sdk/           Xbox XBE Recomp SDK with the changes listed below
tools/analysis xbe_info.py / xbe_funcs.py: header, imports and function survey of an XBE
```

## Building the headless Linux test

Needs clang 18+, CMake, Ninja. Build `xbe_symbols` and `xbe_recomp` from `sdk/tools` (they fetch Zydis and toml++), then:

```
cp /path/to/your/default.xbe assets/
xbe_recomp config/game.toml --xdk sdk/xdk                 # writes generated/game
sdk/harness/build.sh -r . -g generated/game -o out/harness -n sm2
out/harness/sm2 /path/to/game/folder                      # folder with default.xbe and the data folder
```

`REF=N` draws every Nth frame with the CPU reference renderer into `cap/` (see `sdk/harness/README.md`).

## Changes to the SDK

- `runtime/kernel/xboxkrnl.cpp`: `MmQueryAddressProtect` answers from the committed pages instead of always
  `PAGE_READWRITE` (the game walks up its stack in 64 KB steps while pages are read/write).
- `runtime/kernel/xboxkrnl.cpp`: `NtUserIoApcDispatcher` (ReadFileEx/WriteFileEx completions), with
  `RtlNtStatusToDosError`'s table shared as `StatusToDosError`.
- `runtime/xdk/dsound.cpp`: host `IDirectSoundBuffer_SetMode`, `IDirectSoundBuffer_Pause`,
  `CDirectSoundStream_SetMode`, `CDirectSoundStream_SetAllParameters`, `CDirectSoundStream_SetRolloffCurve`
  (guarded, so configs without them still build).

## Config additions by hand

`D3D_Streams`, `D3DDevice_BeginPush`, `D3DDevice_EndPush`, `D3DDevice_SetPalette`, `D3DDevice_Reset` and the
DirectSound methods above and `XGetFilePhysicalSortKey`, located with [XbSymbolDatabase](https://github.com/Cxbx-Reloaded/XbSymbolDatabase) and by
reading the XDK code that uses them. `D3DDevice_Reset` and `XGetFilePhysicalSortKey` are replaced in `src/game/hooks.cpp`.

## Credits

- TGP482 — Xbox XBE Recomp
- Cxbx-Reloaded team — XbSymbolDatabase
- xemu and Cxbx-Reloaded — hardware and API references
