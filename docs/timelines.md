# Timelines: 8-Week Release Plan

```text
┌────────────────────────────────────────────────────────────────────────┐
│  WEEK 1-2: Core Engine & Ingestion (Milestone: 3-Axes Realtime Loop)   │
└────────────────────────────────────────────────────────────────────────┘
                                   │
                                   ▼
┌────────────────────────────────────────────────────────────────────────┐
│  WEEK 3-4: Segmentation Stack (Milestone: Multi-threaded Selection)    │
└────────────────────────────────────────────────────────────────────────┘
                                   │
                                   ▼
┌────────────────────────────────────────────────────────────────────────┐
│  WEEK 5-6: Marching Cubes Engine (Milestone: 3D Phong Mesh Render)     │
└────────────────────────────────────────────────────────────────────────┘
                                   │
                                   ▼
┌────────────────────────────────────────────────────────────────────────┐
│  WEEK 7-8: Refining, STL Export & Multi-OS CI Packaging Verification   │
└────────────────────────────────────────────────────────────────────────┘
```

## Week 1–2: Ingestion & Tri-Axis Workspace View

- **Objective:** Establish the foundational cross-platform build system and display real DICOM imagery slices on screen.
- **Deliverables:**
  - Configure top-level CMake systems to target Ninja build setups on Windows, Linux, and macOS.
  - Boot a Sokol frame canvas rendering the required 3-button immediate toolbar layout.
  - Map raw directory targets, parsing pixel data into continuous 1D float arrays.
  - Set up dynamic GPU texture updating pipelines via `sg_update_image` handling full 3-axes scrolling lookups in real-time.

## Week 3–4: In-Memory Mask Processing Stack

- **Objective:** Allow direct pixel-level manipulation of targeted anatomy.
- **Deliverables:**
  - Allocate the system parallel `mask_buffer` layer.
  - Implement multi-threaded CPU threshold routines optimized via multi-threaded vector blocks.
  - Write cursor-to-world space mathematical matrices to implement real-time 3D paint and erase tool parameters.
  - Design the tracking structures to cache edit sequences for historical undo/redo tracking commands.

## Week 5–6: Marching Cubes Geometry Engine

- **Objective:** Translate raw mask arrays into an interactive 3D polygon interface model.
- **Deliverables:**
  - Embed a single-header Marching Cubes extraction pipeline.
  - Convert the 3D active mask grid into index-aligned array vectors containing native vertices and surface normal configurations.
  - Write standard Sokol shader pipeline containers to render polygon assets with crisp Phong depth components.
  - Bind spatial mouse adjustments to drive viewport scaling, rotational configurations, and tracking offsets.

## Week 7–8: Refining Filters, STL Exporter & Multi-OS CI Locking

- **Objective:** Clean up noisy structural segments, save physical print targets, and automate cross-platform builds.
- **Deliverables:**
  - Add 3D neighborhood median filtering and dilation/erosion passes to eliminate noise and fill internal hollow air gaps.
  - Code the fast binary structural STL data exporter.
  - Implement an embedded SQLite layer to automatically log patient processing sessions.
  - Integrate automated GitHub Actions workflows running automated MSVC, Apple Clang, and GCC compiler tests to lock down long-term open-source development stability.
