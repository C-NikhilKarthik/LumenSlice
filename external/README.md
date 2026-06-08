# External Dependencies

Header-only and vendored dependencies live here, typically as git submodules.
None are committed yet — this file documents the intended layout.

| Directory | Source | Purpose |
| --- | --- | --- |
| `sokol/` | [floooh/sokol](https://github.com/floooh/sokol) | App lifecycle + GPU abstraction (`sokol_app.h`, `sokol_gfx.h`, `sokol_imgui.h`) |
| `imgui/` | [ocornut/imgui](https://github.com/ocornut/imgui) | Immediate-mode UI |
| `eigen/` | [libeigen/eigen](https://gitlab.com/libeigen/eigen) | Header-only linear algebra |

A permissively-licensed single-header **Marching Cubes** implementation will
also be vendored here (`marching_cubes/`) — confirm its license first; see the
note in [`docs/dependencies.md`](../docs/dependencies.md).

DCMTK (`dcmdata`) and SQLite/SQLiteCpp are resolved via the system package
manager or CMake `FetchContent` rather than vendored here. See
[`docs/dependencies.md`](../docs/dependencies.md) for the full dependency matrix,
including where each library is used and license compatibility.
