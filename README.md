# LumenSlice

A high-performance, ultra-lightweight, cross-platform C++ application designed to parse raw DICOM file paths, execute rapid real-time 2D multi-axis segmentation masks, and generate production-ready 3D-printable STL geometries.

Built with a minimalist game-engine philosophy, LumenSlice eliminates the heavy footprint of legacy medical tools, compiling natively into a single binary in under two minutes.

## Core Features

- **Zero-Bloat Architecture:** Powered by Sokol and Dear ImGui — no heavy Qt or VTK/ITK framework overhead.
- **Contiguous 3D Voxel Buffering:** Volumetric data tracks through tightly packed linear memory for rapid computing passes.
- **Real-time Tri-Axis Views:** Instantly inspect, scroll, and contrast-adjust Axial, Coronal, and Sagittal slice dimensions using highly responsive texture pipelines.
- **Multi-Threaded Segmentation:** Harness multi-threaded CPU routines for fast threshold calculations, interactive 3D paint tools, and seed-growing operations.
- **Instant Marching Cubes Extraction:** Extract and preview clean surface meshes inside a hardware-accelerated 3D viewer.
- **Native Binary STL Export:** Direct output of compliant 3D assets optimized for surgical visualization models and 3D printing pipelines.

## Technology Stack

| Concern                        | Library                                                                  |
| ------------------------------ | ------------------------------------------------------------------------ |
| Graphics context handling      | `sokol_app.h` / `sokol_gfx.h` (Metal, DirectX11/12, OpenGL 3.3+, Vulkan) |
| Immediate GUI controls         | Dear ImGui (`sokol_imgui.h`)                                             |
| DICOM input operations         | Minimized DCMTK parsing core (`dcmdata`)                                 |
| Linear transformation routines | Eigen (header-only math engine)                                          |
| Session cache systems          | SQLite (serverless, file-based)                                          |

## Documentation

- [`docs/context.md`](docs/context.md) — deep technical architecture specification
- [`docs/dependencies.md`](docs/dependencies.md) — open-source libraries, where each is used, and licenses
- [`docs/plan.md`](docs/plan.md) — implementation blueprint and requirements
- [`docs/agent.md`](docs/agent.md) — AI code-generation directives
- [`docs/timelines.md`](docs/timelines.md) — 8-week release schedule

## Quick Start Development Guide

### 1. Prerequisites

Ensure your build machine has a modern C++17/20-compliant compiler toolchain installed, alongside CMake and Ninja.

```bash
# Ubuntu / Debian Linux
sudo apt-get install build-essential cmake ninja-build libx11-dev libxi-dev libgl1-mesa-dev

# macOS (via Homebrew)
brew install cmake ninja

# Windows
# Install Visual Studio Community Edition (select "Desktop development with C++")
```

### 2. Clone and Configure Workspace

```bash
git clone --recursive https://github.com/yourusername/LumenSlice.git
cd LumenSlice
mkdir build && cd build

# Configure build scripts via Ninja
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..

# Execute native multi-threaded compilation
ninja
```

> **Status:** Early scaffolding. This repository currently contains the architecture docs and project layout. Source implementation follows the [8-week plan](docs/timelines.md).

## License

LumenSlice is distributed under the open-source [MIT License](LICENSE). Contributions and performance patches from graphics purists and medical imaging innovators are highly encouraged.
