# Context: LumenSlice Architecture Specification

## 1. Engine Core & Subsystems

LumenSlice bypasses monolithic object-oriented hierarchies in favor of a data-oriented, pipeline-driven engine layout. Memory layout boundaries are strictly isolated from the UI rendering layer.

### 1.1 Volumetric Memory Topology

Medical imaging scans are flat structures in hardware. Rather than mapping pointers to rows or slices, data is loaded into a singular, raw contiguous heap vector block.

- **Raw Voxel Array (`voxel_buffer`):** Stored as a 1D array of floats (`float*`). Represents real physical Hounsfield Units (HU) calibrated by processing raw scanner data through linear metadata constants:

  $$HU = (\text{Raw Pixel} \times \text{Rescale Slope}) + \text{Rescale Intercept}$$

- **Segmentation Mask Array (`mask_buffer`):** An identical parallel 1D array of unsigned bytes (`uint8_t*`).
  - `0x00` = Mask background, unselected.
  - `0x01` = Active mask target region (bone, tissue, etc.).

### 1.2 Coordinate Mapping & Geometry Projections

To eliminate structural lookups, index transformations map standard coordinates $(X, Y, Z)$ straight into memory address spaces via pointer arithmetic:

$$\text{Linear Index} = X + (Y \times \text{Width}) + (Z \times \text{Width} \times \text{Height})$$

For rendering across the three orthographic slice paths (Axial, Coronal, Sagittal):

- **Axial (XY Plane):** Sampled via contiguous spatial blocks directly offset by $Z \times \text{Width} \times \text{Height}$.
- **Sagittal (YZ Plane):** Calculated via constant $X$ indices, moving down rows using a jump-multiplier matching the global volume frame width.
- **Coronal (XZ Plane):** Sampled at a fixed $Y$ depth row index, stepping sequentially along depth intervals.

## 2. Dependency Matrix

To maximize performance and keep compile times under two minutes, the project restricts external libraries to targeted open-source packages:

- **Sokol Core Framework (`sokol_app.h`, `sokol_gfx.h`):** Translates C++ abstraction sequences directly to low-level native runtime execution platforms (**Metal** on macOS, **DirectX11/12** on Windows, **OpenGL 3.3 / Vulkan** on Linux).
- **Dear ImGui (`sokol_imgui.h`):** Provides an immediate-mode UI interface. The UI relies entirely on the Sokol canvas event handling mechanism to pass draw calls.
- **DCMTK (DICOM Toolkit Core):** Limited strictly to `dcmdata` components to enable raw metadata file scraping while dropping structural engine layers.
- **Eigen:** Utilized strictly as a header-only utility to compute standard linear transformations, bounding constraints, and camera matrix transformations.
- **SQLiteCpp:** A thin wrapper providing secure multi-threaded tracking to local SQLite client databases for local patient session history cache data.
