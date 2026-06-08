# Dependencies: Buy vs. Build

LumenSlice is "zero-bloat" by **picking the right small libraries**, not by writing
everything from scratch. The rule of thumb: we own the *data-oriented core*
(voxel buffers, masks, segmentation math, STL byte layout) and we **lean on
open source for the undifferentiated heavy lifting** (windowing, GPU backends,
DICOM parsing, linear algebra). This is how an 8-week timeline is realistic.

This document is the single source of truth for the dependency matrix. The
[README](../README.md) and [`context.md`](context.md) summarize it; the layout
of vendored code is described in [`external/README.md`](../external/README.md).

## At a glance

| Library | Role | What it saves us | Used in | Integration | License |
| --- | --- | --- | --- | --- | --- |
| **sokol** (`sokol_app`, `sokol_gfx`, `sokol_glue`) | Window + input + GPU abstraction | Per-OS window creation, swapchains, and a Metal/D3D11/GL/Vulkan backend each | `src/main.cpp`, `src/visualization/` | git submodule (header-only) | zlib/libpng |
| **Dear ImGui** + `sokol_imgui.h` | Immediate-mode UI | An entire retained-mode widget toolkit (vs. Qt) | `src/main.cpp`, toolbar/panels | git submodule | MIT |
| **DCMTK** (`dcmdata` only) | DICOM parsing | A correct DICOM tag/VR parser, transfer-syntax handling, rescale metadata | `src/io/dicom_loader.cpp` | system package / FetchContent | BSD-3-Clause-like (DCMTK) |
| **Eigen** | Linear algebra | Vectors, matrices, camera/transform math, SVD for orientation sorting | `src/visualization/`, `src/segmentation/` raycast | git submodule (header-only) | MPL-2.0 |
| **SQLite** (+ optional **SQLiteCpp**) | Session cache | A serverless embedded DB engine | `src/core/database.cpp` | amalgamation / FetchContent | Public domain (SQLite), MIT (SQLiteCpp) |
| **Marching Cubes tables** | Isosurface extraction | The 256-entry edge/triangle lookup tables + traversal | `src/visualization/` (mesh gen) | vendored single-header (see below) | see note |

## Why each one

### sokol — the platform + GPU layer

`sokol_app.h` gives us one entry point that opens a native window, pumps the
event loop, and hands us a ready GPU swapchain on **Metal (macOS), D3D11
(Windows), and OpenGL/Vulkan (Linux)**. `sokol_gfx.h` is the cross-backend
graphics API we draw through. Without it we'd be writing and maintaining four
separate windowing + rendering backends — easily the single biggest time sink
in the project. Header-only, no build step, trivial to vendor.

- Used by: the app lifecycle in `src/main.cpp` and every render path in
  `src/visualization/` (slice textures via `sg_update_image`, the 3D mesh
  pipeline).

### Dear ImGui (+ `sokol_imgui.h`) — the UI

Every panel, slider (HU window/level), and the 3-axis toolbar is an ImGui
immediate-mode call. `sokol_imgui.h` is the glue that feeds ImGui's draw lists
into `sokol_gfx`. This replaces a retained-mode toolkit like Qt entirely, which
keeps the binary tiny and the build under two minutes.

- Used by: `src/main.cpp` toolbar/panels; tool parameter UI for the
  segmentation stack.

### DCMTK (`dcmdata` subset) — DICOM input

DICOM is deceptively hard: tag dictionaries, value representations, transfer
syntaxes, and rescale slope/intercept. We use **only `dcmdata`** to read pixel
data and the metadata we need — Rescale Slope/Intercept `(0028,1052/1053)`,
Pixel Spacing `(0028,0030)`, Image Orientation `(0020,0037)`, Image Position
`(0020,0032)` — then drop everything into our own flat `float` buffer. We
deliberately do **not** pull DCMTK's network or image-toolkit layers.

- Used by: `src/io/dicom_loader.cpp` (folder crawl, `"DICM"` signature check at
  byte 128, tag extraction, geometric Z-sorting).

### Eigen — math

Header-only linear algebra for camera/view/projection matrices, the
cursor-to-world raycast that maps a viewport click into a voxel index, and
orientation-vector math used when sorting slices. Saves us a hand-rolled,
bug-prone matrix library.

- Used by: `src/visualization/` (camera + transforms),
  `src/segmentation/` (raycast index mapping).

### SQLite (+ SQLiteCpp) — session cache

A serverless, single-file database to log patient processing sessions locally.
SQLite ships as a public-domain amalgamation (one `.c` + one header); SQLiteCpp
is a thin, optional RAII C++ wrapper for safer prepared statements.

- Used by: `src/core/database.cpp`.

### Marching Cubes — isosurface extraction (read the license note)

The mask volume becomes a printable mesh via Marching Cubes. The hard part is
not the loop — it's the canonical **256-entry edge and triangle lookup tables**
(Lorensen & Cline). Plan: vendor a **single-header** implementation under
`external/` and feed it our `mask_buffer`, emitting vertices + normals straight
into the Sokol mesh pipeline and the STL exporter.

> **Note on `nihaljn/marching-cubes`:** it's a clean C++ reference (reads a
> scalar field, writes PLY) and is useful to study, but as of writing the repo
> **declares no license**, so we cannot copy its source into an MIT project.
> Safe paths: (a) adapt the **public-domain Lorensen/Bourke tables** directly,
> or (b) vendor a permissively licensed single-header such as a
> `MarchingCubes.h` released under MIT/zlib/public-domain. Confirm the license
> before adding anything to `external/`.

- Used by: `src/visualization/` mesh generation, feeding `src/io/stl_exporter.cpp`.

## How dependencies are wired in

Three integration styles, chosen per library:

1. **git submodules under `external/`** — for header-only or vendored source we
   want pinned to an exact commit: `sokol/`, `imgui/`, `eigen/`, and the
   marching-cubes header. This is why the clone instructions use
   `git clone --recursive`.
2. **CMake `FetchContent`** — convenient pinned fetch at configure time; a good
   option for SQLiteCpp and (where no system package exists) DCMTK.
3. **System package** — DCMTK is large; preferring the OS package
   (`libdcmtk-dev`, `brew install dcmtk`, vcpkg) keeps our build fast.

The top-level `CMakeLists.txt` (added with Week 1 work) will own this wiring.
Until then, `external/` holds placeholder directories only.

## License compatibility (we ship under MIT)

| Dependency | License | MIT-compatible? |
| --- | --- | --- |
| sokol | zlib/libpng | ✅ |
| Dear ImGui | MIT | ✅ |
| Eigen | MPL-2.0 | ✅ (file-level copyleft; fine when used unmodified) |
| DCMTK | BSD-style (DCMTK) | ✅ (preserve copyright notice) |
| SQLite | Public domain | ✅ |
| SQLiteCpp | MIT | ✅ |
| `nihaljn/marching-cubes` | **none declared** | ❌ do not copy — see note above |

Keep this table honest as dependencies are added: a "zero-bloat" promise is also
a promise that every byte we ship has a known, compatible license.
