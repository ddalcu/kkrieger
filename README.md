# .kkrieger for macOS and the browser

**Play: https://ddalcu.github.io/kkrieger/**

A port of .theprodukkt's 96k first-person shooter .kkrieger (2004) to
macOS/Metal and WebAssembly/WebGL2. Source: `fr_public/werkkzeug3_kkrieger` (BSD-licensed, Farbrausch/.theprodukkt,
https://github.com/farbrausch/fr_public), data `data/kkrieger3383.kx`.

## Status

**Playable on macOS/Apple Silicon and in the browser.** Loading bar -> press
Space -> menu (Enter / click) -> game, with textures, lighting, stencil
shadows, post-processing, HUD text, music and sound effects.

- `make` builds `kkrieger-mac` (Metal).
- `make web` builds `web/index.html` + `.js` + `.wasm` (Emscripten, WebGL2).
  Serve the directory over http, e.g. `python3 -m http.server -d web`, and
  open http://localhost:8000. Browsers start audio only after the first key
  press or click.

Controls (as the original): W/A/S/D move, mouse look (click to capture the
mouse; Cmd-Tab on Mac or Esc in the browser releases it), left mouse fire,
Space jump, 1-5 weapons, Esc menu, Cmd-Q quit (Mac).

The game renders at its in-game resolution option (default 800x600) and is
letterboxed into the window / canvas.

## Layout

```
Makefile               PLATFORM=mac (default) -> obj/, PLATFORM=web -> obj-web/
platform/
  port.hpp             MSVC->clang shim (force-included), sGATHERARGS
  callwrap.hpp         operator handler ABI shim (x86 dword slots -> typed args)
  platform.hpp         what the per-platform files provide to each other
  common.cpp           platform-independent sSystem_: clock, files, sample
                       mixer, material plumbing, Windows runtime shims
  render.hpp           backend interface (b* functions)
  render_common.cpp    D3D9 semantics: render states, setups, constants,
                       geometry/texture handles, mip chains, loading screen
  shader_d3d9.cpp/.hpp D3D9 vs/ps 1.x (and simple 2.0) bytecode -> MSL or GLSL ES
  render_metal.mm      Metal backend
  render_gl.cpp        WebGL2 backend
  main_mac.mm          Cocoa window, event pump, keyboard/mouse, main loop
  start_mac.mm         CoreText host font, CoreAudio
  main_web.cpp         canvas, keyboard, pointer lock, rAF main loop (Asyncify)
  start_web.cpp        canvas-2D host font, Web Audio
  shell.html           page template for the web build
src/                   player sources (patched, see below)
src/v2/                V2 synth core in C++ (fr_public/v2/synth_core.cpp)
data/                  kkrieger3383.kx: the game's export (embedded at build time)
.github/workflows/     builds the web version and publishes it to GitHub Pages
```

## How the platform layer works

- **Rendering.** Materials are D3D9 render-state streams plus vs_1_1/ps_1_1
  bytecode. `shader_d3d9.cpp` translates each setup's bytecode on first use
  (co-issue, 1.x source/result modifiers, ps constant clamping, vertex fog,
  alpha test, fixed-function stages for `sSimpleMaterial`) into MSL or GLSL ES
  3.00. Backends derive pipeline, depth-stencil (two-sided stencil for shadow
  volumes) and sampler state from the D3D state array at draw time and cache
  it. GLSL flips y and remaps clip z to GL conventions.
- **Geometry.** Metal: static Geo handles get their own buffers, dynamic ones
  come from a per-frame arena (3 frames in flight). WebGL: each handle keeps a
  CPU copy that is uploaded when it changes.
- **Main loop.** The engine's blocking loops (loading bar, wait for key, game
  loop) call `sPlatformPump`; on the web it awaits `requestAnimationFrame`
  through Asyncify.
- **Sound.** The x86 asm synth is replaced by ryg's C++ `synth_core.cpp`.
  CoreAudio / a Web Audio script processor pull 16-bit stereo through the
  game's sound handler; samples are mixed on top by `sMixSamples`.

## Fixes to the original sources (all marked `port:`)

Data / format:
- `kdoc.cpp`: the .kx is the 2004 export: no flags dword/demo name, sample
  block always present, 1-byte class ids. Viewport (0xf0) and Cylinder (0x82)
  gained parameters later; they are widened with the editor defaults.
  Bounds checks on class index and input deltas.
- `mainplayer.cpp`: the songs are stored in the composer's V2M version and are
  converted to the current patch layout (`sConvertV2M`) before playback.

64-bit / non-x86 / non-MSVC:
- `KOp::Init` sized pointer arrays in dwords; `KOp::Exec` packed params as
  dwords into 64-bit slots; packing pointer smuggled through `WheelSpeed`.
- Variadic handlers read `(&first)[i]` from the x86 stack (Bitmap_Merge,
  Material_Add, Scene_Add, Mesh_Add, MinMesh_Add, IPP_Select), as did
  Mesh_Cube's `(&tx)[j]` and Scene_Walk's `(&l0)[i]`; scene handlers read
  `&s.x` as nine floats across three params (now one `sFSRT`).
- Engine paint-job arena sized for 32-bit structs.
- Hand-written MMX: BilinearFilter lost its 0x8000 bias (all resampling ops
  returned black), Bitmap_Inner color range used data[1] instead of
  data[1]-data[0].
- The code indexes struct members as arrays (`(&v.x)[i]`), which MSVC
  tolerates; clang's type-based alias analysis drops such stores (in the wasm
  build Mesh_Cube lost its y/z face mapping, leaving holes in the level), so
  everything is built with `-fno-strict-aliasing`.

Player build (`sINTRO`) paths the 2004 player needed:
- `Engine_::ProcessPortals` (kkrieger paints its level through sectors).
- `Bitmap_Text` font pages (menu and HUD text).

Upstream bugs:
- `KKriegerGame::SetPainter` walked into freed caches.
- `Bitmap_Twirl` CSLookup read one past its table.
- `synth_core.cpp` `init` cleared `sizeof(this)` bytes.
- texture handle 0 is used as a placeholder by GenOverlayManager; the backend
  reserves it so ref counting cannot free a real texture there.

## License

The original code is BSD-licensed (see LICENSE.txt); the V2 synth was placed
in the public domain by Tammo "kb" Hinrichs. The port follows the same terms.
